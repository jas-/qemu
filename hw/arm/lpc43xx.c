/*
 * NXP LPC43xx SoC / Machine emulation for QEMU
 *
 * Supports four machine types:
 *   lpc4350  – LPC4350 (Cortex-M4 @ 204 MHz + M0, ETH, USB0/1, SDMMC)
 *   lpc4330  – LPC4330 (Cortex-M4 @ 204 MHz + M0, USB0/1, no ETH)
 *   lpc4320  – LPC4320 (Cortex-M4 @ 180 MHz + M0, USB0, no crypto)
 *   lpc4310  – LPC4310 (Cortex-M4 @ 180 MHz, single-core)
 *
 * An additional convenience target:
 *   hackrf-portapack  – LPC4320 pre-configured for HackRF+PortaPack Mayhem
 *                       firmware (correct SPIFI window, dual-core, no ETH)
 *
 * Memory layout (UM10503 §2.1 Memory Map):
 *
 *   0x00000000 – 0x0FFFFFFF  : Internal code space
 *     0x10000000 – 0x10007FFF  SRAM0  (32 kB, M4 local)
 *     0x10080000 – 0x10089FFF  SRAM1  (40 kB)
 *     0x10400000 – 0x1040FFFF  Boot ROM (64 kB)
 *     0x14000000 – 0x14FFFFFF  SPIFI   (16 MB window)
 *   0x20000000 – 0x2FFFFFFF  : Data SRAM
 *     0x20000000 – 0x20007FFF  SRAM2  (32 kB, AHB)
 *     0x20008000 – 0x2000BFFF  SRAM3  (16 kB, M0 local)
 *   0x40000000 – 0x400FFFFF  : AHB peripherals
 *   0x40050000              : CGU
 *   0x40051000              : CCU1
 *   0x40052000              : CCU2
 *   0x40043000              : CREG
 *   0x40086000              : SCU
 *
 * Copyright (c) 2024 QEMU Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * References:
 *   NXP UM10503 LPC43xx User Manual Rev 1.9
 *   hw/arm/mps2.c (Cortex-M4 board example)
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qemu/cutils.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "hw/arm/boot.h"
#include "target/arm/cpu.h"
#include "hw/arm/armv7m.h"
#include "hw/arm/machines-qom.h"
#include "hw/core/boards.h"
#include "hw/misc/unimp.h"
#include "hw/char/pl011.h"
#include "hw/ssi/pl022.h"
#include "hw/core/qdev-clock.h"
#include "system/address-spaces.h"
#include "system/system.h"
#include "hw/core/qdev-properties.h"
#include "qom/object.h"
#include "target/arm/arm-powerctl.h"

/* Local peripheral headers */
#include "hw/arm/lpc43xx.h"
#include "hw/misc/lpc43xx_cgu.h"
#include "hw/misc/lpc43xx_ccu.h"
#include "hw/misc/lpc43xx_scu_creg.h"

/* =========================================================================
 * Machine class / state structures
 * ========================================================================= */

struct LPC43XXMachineClass {
    MachineClass   parent;
    LPC43XXVariant variant;
    uint32_t       chip_id;
    bool           has_m0;
    bool           has_ethernet;
    const char    *desc;
};

struct LPC43XXMachineState {
    MachineState  parent;

    /* Cortex-M4 primary core */
    ARMv7MState   m4;

    /* Cortex-M0 application core (LPC4350/30/20 only) */
    ARMv7MState   m0;

    /* Internal SRAM banks */
    MemoryRegion  m0_mem;
    MemoryRegion  sram0;   /* 32 kB  @ 0x10000000 */
    MemoryRegion  sram1;   /* 40 kB  @ 0x10080000 */
    MemoryRegion  sram2;   /* 32 kB  @ 0x20000000 */
    MemoryRegion  sram3;   /* 16 kB  @ 0x20008000 (M0 local) */

    /* Boot ROM image (loaded from -bios, or zeroed stub) */
    MemoryRegion  boot_rom;

    /* SPIFI flash window – backed by a RAM region for emulation */
    MemoryRegion  spifi_mem;
    MemoryRegion  shadow_mem;


    /* Clock subsystem */
    LPC43XXCGUState  cgu;
    LPC43XXCCUState  ccu1;
    LPC43XXCCUState  ccu2;

    /* System / pad control */
    LPC43XXSCUState  scu;
    LPC43XXCREGState creg;

    /* Clock objects */
    Clock *sysclk;   /* M4 core clock */
    Clock *refclk;   /* 1 MHz systick reference */
};

#define TYPE_LPC43XX_MACHINE          "lpc43xx"
OBJECT_DECLARE_TYPE(LPC43XXMachineState, LPC43XXMachineClass, LPC43XX_MACHINE)

/* =========================================================================
 * Clocks
 * ========================================================================= */

/* LPC4350/30 run at up to 204 MHz; LPC4320/10 at 180 MHz.
 * We default to 204 MHz for all variants; the CGU emulation handles
 * any firmware-requested changes without modelling true PLL behaviour. */
#define LPC43XX_SYSCLK_HZ   204000000UL
#define LPC43XX_REFCLK_HZ   (1 * 1000 * 1000)

/* =========================================================================
 * Helper: map a region and add to system memory
 * ========================================================================= */
static void make_ram(MemoryRegion *mr, const char *name,
                     hwaddr base, hwaddr size)
{
    memory_region_init_ram(mr, NULL, name, size, &error_fatal);
    memory_region_add_subregion(get_system_memory(), base, mr);
}

/* =========================================================================
 * Helper: create and map a PL011 UART
 * ========================================================================= */
static void create_uart(ARMv7MState *m4, int uart_idx,
                        hwaddr base, int irq_rx, int irq_tx,
                        uint64_t pclk_hz)
{
    DeviceState *dev;
    SysBusDevice *sbd;

    dev = qdev_new(TYPE_PL011);
    sbd = SYS_BUS_DEVICE(dev);
    qdev_prop_set_chr(dev, "chardev", serial_hd(uart_idx));
    sysbus_realize_and_unref(sbd, &error_fatal);
    sysbus_mmio_map(sbd, 0, base);
    /* PL011 IRQ 0 = combined UART IRQ */
    sysbus_connect_irq(sbd, 0,
                       qdev_get_gpio_in(DEVICE(m4), irq_rx));
    (void)irq_tx; /* TX irq wired to same line for simplicity */
    (void)pclk_hz;
}

/* =========================================================================
 * SoC initialisation
 * ========================================================================= */
static void lpc43xx_init(MachineState *machine)
{
    LPC43XXMachineState  *s   = LPC43XX_MACHINE(machine);
    LPC43XXMachineClass  *mc  = LPC43XX_MACHINE_GET_CLASS(machine);
    MemoryRegion         *sys = get_system_memory();
    DeviceState          *m4dev;

    /* ------------------------------------------------------------------ */
    /* Clocks                                                               */
    /* ------------------------------------------------------------------ */
    s->sysclk = clock_new(OBJECT(machine), "SYSCLK");
    clock_set_hz(s->sysclk, LPC43XX_SYSCLK_HZ);

    s->refclk = clock_new(OBJECT(machine), "REFCLK");
    clock_set_hz(s->refclk, LPC43XX_REFCLK_HZ);

    /* ------------------------------------------------------------------ */
    /* Internal SRAM banks                                                  */
    /* ------------------------------------------------------------------ */
    make_ram(&s->sram0, "lpc43xx.sram0",
             LPC43XX_SRAM0_BASE, LPC43XX_SRAM0_SIZE);
    make_ram(&s->sram1, "lpc43xx.sram1",
             LPC43XX_SRAM1_BASE, LPC43XX_SRAM1_SIZE);
    make_ram(&s->sram2, "lpc43xx.sram2",
             LPC43XX_SRAM2_BASE, LPC43XX_SRAM2_SIZE);
    make_ram(&s->sram3, "lpc43xx.sram3",
             LPC43XX_SRAM3_BASE, LPC43XX_SRAM3_SIZE);

    /* ------------------------------------------------------------------ */
    /* Boot ROM                                                             */
    /* ------------------------------------------------------------------ */
    memory_region_init_ram(&s->boot_rom, NULL,
                           "lpc43xx.bootrom",
                           LPC43XX_BOOT_ROM_SIZE, &error_fatal);
    memory_region_add_subregion(sys, LPC43XX_BOOT_ROM_BASE, &s->boot_rom);

    /* ------------------------------------------------------------------ */
    /* SPIFI flash window (16 MB, writable RAM-backed for emulation)        */
    /* ------------------------------------------------------------------ */
    make_ram(&s->spifi_mem, "lpc43xx.spifi",
             LPC43XX_SPIFI_BASE, LPC43XX_SPIFI_SIZE);
    make_ram(&s->shadow_mem, "lpc43xx.shadow", 0x00000000, LPC43XX_SPIFI_SIZE);

    /* ------------------------------------------------------------------ */
    /* Primary Cortex-M4 core                                               */
    /* ------------------------------------------------------------------ */
    object_initialize_child(OBJECT(s), "m4", &s->m4, TYPE_ARMV7M);
    m4dev = DEVICE(&s->m4);

    qdev_prop_set_uint32(m4dev, "num-irq",    LPC43XX_NUM_IRQ);
    qdev_prop_set_string(m4dev, "cpu-type",
                         ARM_CPU_TYPE_NAME("cortex-m4"));
    qdev_prop_set_bit(m4dev, "enable-bitband", true);
    qdev_connect_clock_in(m4dev, "cpuclk", s->sysclk);
    qdev_connect_clock_in(m4dev, "refclk",  s->refclk);
    object_property_set_link(OBJECT(&s->m4), "memory",
                             OBJECT(sys), &error_abort);
    sysbus_realize(SYS_BUS_DEVICE(&s->m4), &error_fatal);

    /* ------------------------------------------------------------------ */
    /* Secondary Cortex-M0 core (dual-core variants)                        */
    /* ------------------------------------------------------------------ */
    if (mc->has_m0 && machine->smp.cpus > 1) {
        DeviceState *m0dev;

        object_initialize_child(OBJECT(s), "m0", &s->m0, TYPE_ARMV7M);
        m0dev = DEVICE(&s->m0);

        qdev_prop_set_uint32(m0dev, "num-irq",    32);
        qdev_prop_set_uint32(m0dev, "init-svtor", 0x000f1878);
        qdev_prop_set_string(m0dev, "cpu-type",
                             ARM_CPU_TYPE_NAME("cortex-m0"));
        qdev_prop_set_bit(m0dev, "enable-bitband", false);
        qdev_connect_clock_in(m0dev, "cpuclk", s->sysclk);
        qdev_connect_clock_in(m0dev, "refclk",  s->refclk);
        memory_region_init_alias(&s->m0_mem, OBJECT(s), "lpc43xx.m0-mem",
                                 sys, 0, UINT32_MAX);
        object_property_set_link(OBJECT(&s->m0), "memory",
                                 OBJECT(&s->m0_mem), &error_abort);
        sysbus_realize(SYS_BUS_DEVICE(&s->m0), &error_fatal);
        arm_set_cpu_off(ARM_CPU(s->m0.cpu)->mp_affinity);
    }

    /* ------------------------------------------------------------------ */
    /* Clock Generation Unit (CGU)                                          */
    /* ------------------------------------------------------------------ */
    object_initialize_child(OBJECT(s), "cgu", &s->cgu, TYPE_LPC43XX_CGU);
    sysbus_realize(SYS_BUS_DEVICE(&s->cgu), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->cgu), 0, LPC43XX_CGU_BASE);

    /* ------------------------------------------------------------------ */
    /* Clock Control Units (CCU1, CCU2)                                     */
    /* ------------------------------------------------------------------ */
    object_initialize_child(OBJECT(s), "ccu1", &s->ccu1, TYPE_LPC43XX_CCU);
    s->ccu1.index = 1;
    sysbus_realize(SYS_BUS_DEVICE(&s->ccu1), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->ccu1), 0, LPC43XX_CCU1_BASE);

    object_initialize_child(OBJECT(s), "ccu2", &s->ccu2, TYPE_LPC43XX_CCU);
    s->ccu2.index = 2;
    sysbus_realize(SYS_BUS_DEVICE(&s->ccu2), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->ccu2), 0, LPC43XX_CCU2_BASE);

    /* ------------------------------------------------------------------ */
    /* System Control Unit (SCU)                                            */
    /* ------------------------------------------------------------------ */
    object_initialize_child(OBJECT(s), "scu", &s->scu, TYPE_LPC43XX_SCU);
    sysbus_realize(SYS_BUS_DEVICE(&s->scu), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->scu), 0, LPC43XX_SCU_BASE);

    /* ------------------------------------------------------------------ */
    /* Configuration Registers (CREG)                                       */
    /* ------------------------------------------------------------------ */
    object_initialize_child(OBJECT(s), "creg", &s->creg, TYPE_LPC43XX_CREG);
    s->creg.chip_id = mc->chip_id;
    sysbus_realize(SYS_BUS_DEVICE(&s->creg), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->creg), 0, LPC43XX_CREG_BASE);

    /* ------------------------------------------------------------------ */
    /* UARTs  (USART0..3 / UART1) – use PL011 as compatible UART           */
    /* ------------------------------------------------------------------ */
    create_uart(&s->m4, 0, LPC43XX_USART0_BASE,
                LPC43XX_IRQ_USART0, LPC43XX_IRQ_USART0, LPC43XX_SYSCLK_HZ);
    create_uart(&s->m4, 1, LPC43XX_UART1_BASE,
                LPC43XX_IRQ_UART1, LPC43XX_IRQ_UART1, LPC43XX_SYSCLK_HZ);
    create_uart(&s->m4, 2, LPC43XX_USART2_BASE,
                LPC43XX_IRQ_USART2, LPC43XX_IRQ_USART2, LPC43XX_SYSCLK_HZ);
    create_uart(&s->m4, 3, LPC43XX_USART3_BASE,
                LPC43XX_IRQ_USART3, LPC43XX_IRQ_USART3, LPC43XX_SYSCLK_HZ);

    /* ------------------------------------------------------------------ */
    /* SSP0 / SSP1  (PL022 SPI controllers)                                */
    /* ------------------------------------------------------------------ */
    sysbus_create_simple(TYPE_PL022, LPC43XX_SSP0_BASE,
                         qdev_get_gpio_in(m4dev, LPC43XX_IRQ_SSP0));
    sysbus_create_simple(TYPE_PL022, LPC43XX_SSP1_BASE,
                         qdev_get_gpio_in(m4dev, LPC43XX_IRQ_SSP1));

    /* ------------------------------------------------------------------ */
    /* Unimplemented peripheral stubs (prevent guest aborts on access)      */
    /* ------------------------------------------------------------------ */
    create_unimplemented_device("lpc43xx.wwdt",    LPC43XX_WWDT_BASE,   0x1000);
    create_unimplemented_device("lpc43xx.timer0",  LPC43XX_TIMER0_BASE, 0x1000);
    create_unimplemented_device("lpc43xx.timer1",  LPC43XX_TIMER1_BASE, 0x1000);
    create_unimplemented_device("lpc43xx.timer2",  LPC43XX_TIMER2_BASE, 0x1000);
    create_unimplemented_device("lpc43xx.timer3",  LPC43XX_TIMER3_BASE, 0x1000);
    create_unimplemented_device("lpc43xx.i2c0",    LPC43XX_I2C0_BASE,   0x1000);
    create_unimplemented_device("lpc43xx.i2c1",    LPC43XX_I2C1_BASE,   0x1000);
    create_unimplemented_device("lpc43xx.i2s0",    LPC43XX_I2S0_BASE,   0x1000);
    create_unimplemented_device("lpc43xx.i2s1",    LPC43XX_I2S1_BASE,   0x1000);
    create_unimplemented_device("lpc43xx.adc0",    LPC43XX_ADC0_BASE,   0x1000);
    create_unimplemented_device("lpc43xx.adc1",    LPC43XX_ADC1_BASE,   0x1000);
    create_unimplemented_device("lpc43xx.dac",     LPC43XX_DAC_BASE,    0x1000);
    create_unimplemented_device("lpc43xx.gpio",    LPC43XX_GPIO_BASE,   0x4000);
    create_unimplemented_device("lpc43xx.sgpio",   LPC43XX_SGPIO_BASE,  0x1000);
    create_unimplemented_device("lpc43xx.spi",     LPC43XX_SPI_BASE,    0x1000);
    create_unimplemented_device("lpc43xx.sct",     LPC43XX_SCT_BASE,    0x1000);
    create_unimplemented_device("lpc43xx.gpdma",   LPC43XX_GPDMA_BASE,  0x1000);
    create_unimplemented_device("lpc43xx.spifictrl",LPC43XX_SPIFI_CTRL_BASE,0x1000);
    create_unimplemented_device("lpc43xx.sdmmc",   LPC43XX_SDMMC_BASE,  0x1000);
    create_unimplemented_device("lpc43xx.emc",     LPC43XX_EMC_BASE,    0x1000);
    create_unimplemented_device("lpc43xx.usb0",    LPC43XX_USB0_BASE,   0x1000);
    create_unimplemented_device("lpc43xx.usb1",    LPC43XX_USB1_BASE,   0x1000);
    create_unimplemented_device("lpc43xx.lcd",     LPC43XX_LCD_BASE,    0x1000);
    create_unimplemented_device("lpc43xx.rgu",     LPC43XX_RGU_BASE,    0x1000);
    create_unimplemented_device("lpc43xx.pmc",     LPC43XX_PMC_BASE,    0x1000);

    if (mc->has_ethernet) {
        create_unimplemented_device("lpc43xx.eth",
                                    LPC43XX_ETHERNET_BASE, 0x4000);
    }

    /* ------------------------------------------------------------------ */
    /* Load kernel / firmware into SPIFI window (primary load address)      */
    /* ------------------------------------------------------------------ */
    armv7m_load_kernel(s->m4.cpu, machine->kernel_filename,
                       0x00000000, LPC43XX_SPIFI_SIZE);
}

/* =========================================================================
 * Machine class initialisers
 * ========================================================================= */

static void lpc43xx_machine_class_init(ObjectClass *oc, const void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->init             = lpc43xx_init;
    mc->min_cpus         = 1;
    mc->max_cpus         = 2;   /* M4 + M0 */
    mc->default_cpus     = 1;
    mc->default_ram_size = 0;   /* no external RAM by default */
    mc->default_ram_id   = NULL;
    mc->no_parallel      = 1;
}

/* ---- LPC4350 ---------------------------------------------------------- */
static void lpc4350_class_init(ObjectClass *oc, const void *data)
{
    LPC43XXMachineClass *lmc = LPC43XX_MACHINE_CLASS(oc);
    MachineClass        *mc  = MACHINE_CLASS(oc);

    mc->desc             = "NXP LPC4350 (Cortex-M4 + M0, ETH/USB/SDMMC)";
    lmc->variant         = LPC43XX_VARIANT_4350;
    lmc->chip_id         = LPC4350_CHIP_ID;
    lmc->has_m0          = true;
    lmc->has_ethernet    = true;
}

/* ---- LPC4330 ---------------------------------------------------------- */
static void lpc4330_class_init(ObjectClass *oc, const void *data)
{
    LPC43XXMachineClass *lmc = LPC43XX_MACHINE_CLASS(oc);
    MachineClass        *mc  = MACHINE_CLASS(oc);

    mc->desc             = "NXP LPC4330 (Cortex-M4 + M0, USB0/1)";
    lmc->variant         = LPC43XX_VARIANT_4330;
    lmc->chip_id         = LPC4330_CHIP_ID;
    lmc->has_m0          = true;
    lmc->has_ethernet    = false;
}

/* ---- LPC4320 ---------------------------------------------------------- */
static void lpc4320_class_init(ObjectClass *oc, const void *data)
{
    LPC43XXMachineClass *lmc = LPC43XX_MACHINE_CLASS(oc);
    MachineClass        *mc  = MACHINE_CLASS(oc);

    mc->desc             = "NXP LPC4320 (Cortex-M4 + M0, USB0, no crypto)";
    lmc->variant         = LPC43XX_VARIANT_4320;
    lmc->chip_id         = LPC4320_CHIP_ID;
    lmc->has_m0          = true;
    lmc->has_ethernet    = false;
}

/* ---- LPC4310 ---------------------------------------------------------- */
static void lpc4310_class_init(ObjectClass *oc, const void *data)
{
    LPC43XXMachineClass *lmc = LPC43XX_MACHINE_CLASS(oc);
    MachineClass        *mc  = MACHINE_CLASS(oc);

    mc->desc             = "NXP LPC4310 (Cortex-M4, single-core)";
    lmc->variant         = LPC43XX_VARIANT_4310;
    lmc->chip_id         = LPC4310_CHIP_ID;
    lmc->has_m0          = false;
    lmc->has_ethernet    = false;
}

/* ---- HackRF + PortaPack (LPC4320 target) ------------------------------- */
static void hackrf_portapack_class_init(ObjectClass *oc, const void *data)
{
    LPC43XXMachineClass *lmc = LPC43XX_MACHINE_CLASS(oc);
    MachineClass        *mc  = MACHINE_CLASS(oc);

    mc->desc             = "HackRF One + PortaPack Mayhem "
                           "(NXP LPC4320, Cortex-M4+M0, SPIFI flash)";
    lmc->variant         = LPC43XX_VARIANT_4320;
    lmc->chip_id         = LPC4320_CHIP_ID;
    lmc->has_m0          = true;
    lmc->has_ethernet    = false;
}

/* =========================================================================
 * TypeInfo table
 * ========================================================================= */

static const TypeInfo lpc43xx_machine_info = {
    .name          = TYPE_LPC43XX_MACHINE,
    .parent        = TYPE_MACHINE,
    .abstract      = true,
    .instance_size = sizeof(LPC43XXMachineState),
    .class_size    = sizeof(LPC43XXMachineClass),
    .class_init    = lpc43xx_machine_class_init,
};

static const TypeInfo lpc4350_machine_info = {
    .name       = TYPE_LPC4350_MACHINE,
    .parent     = TYPE_LPC43XX_MACHINE,
    .class_init = lpc4350_class_init,
    .interfaces = arm_machine_interfaces,
};

static const TypeInfo lpc4330_machine_info = {
    .name       = TYPE_LPC4330_MACHINE,
    .parent     = TYPE_LPC43XX_MACHINE,
    .class_init = lpc4330_class_init,
    .interfaces = arm_machine_interfaces,
};

static const TypeInfo lpc4320_machine_info = {
    .name       = TYPE_LPC4320_MACHINE,
    .parent     = TYPE_LPC43XX_MACHINE,
    .class_init = lpc4320_class_init,
    .interfaces = arm_machine_interfaces,
};

static const TypeInfo lpc4310_machine_info = {
    .name       = TYPE_LPC4310_MACHINE,
    .parent     = TYPE_LPC43XX_MACHINE,
    .class_init = lpc4310_class_init,
    .interfaces = arm_machine_interfaces,
};

static const TypeInfo hackrf_portapack_machine_info = {
    .name       = TYPE_HACKRF_PORTAPACK_MACHINE,
    .parent     = TYPE_LPC43XX_MACHINE,
    .class_init = hackrf_portapack_class_init,
    .interfaces = arm_machine_interfaces,
};

static void lpc43xx_machine_init(void)
{
    type_register_static(&lpc43xx_machine_info);
    type_register_static(&lpc4350_machine_info);
    type_register_static(&lpc4330_machine_info);
    type_register_static(&lpc4320_machine_info);
    type_register_static(&lpc4310_machine_info);
    type_register_static(&hackrf_portapack_machine_info);
}
type_init(lpc43xx_machine_init)
