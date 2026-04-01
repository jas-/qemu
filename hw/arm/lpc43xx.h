/*
 * NXP LPC43xx SoC series emulation
 *
 * Supports LPC4350/4330/4320/4310 (Cortex-M4 + Cortex-M0 dual-core)
 * as used in the HackRF One / PortaPack Mayhem platform.
 *
 * Copyright (c) 2024 QEMU Contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 *
 * Reference: NXP UM10503 LPC43xx User Manual Rev 1.9
 */

#ifndef HW_ARM_LPC43XX_H
#define HW_ARM_LPC43XX_H

#include "hw/arm/armv7m.h"
#include "hw/char/pl011.h"
#include "hw/ssi/pl022.h"
#include "hw/i2c/arm_sbcon_i2c.h"
#include "hw/misc/unimp.h"
#include "hw/core/irq.h"
#include "qom/object.h"

#include "target/arm/arm-powerctl.h"

/* -----------------------------------------------------------------------
 * LPC43xx variant identifiers
 * ----------------------------------------------------------------------- */
typedef enum {
    LPC43XX_VARIANT_4350, /* Cortex-M4 200 MHz + M0 with ETH/USB/SDMMC */
    LPC43XX_VARIANT_4330, /* Cortex-M4 200 MHz + M0 no ETH              */
    LPC43XX_VARIANT_4320, /* Cortex-M4 180 MHz + M0 no crypto           */
    LPC43XX_VARIANT_4310, /* Cortex-M4 180 MHz  single-core             */
} LPC43XXVariant;

/* -----------------------------------------------------------------------
 * Memory-map constants  (UM10503 §2)
 * ----------------------------------------------------------------------- */

/* Flash / ROM */
#define LPC43XX_BOOT_ROM_BASE   0x10400000UL
#define LPC43XX_BOOT_ROM_SIZE   (64 * 1024)

/* Internal SRAM banks */
#define LPC43XX_SRAM0_BASE      0x10000000UL   /* 32 kB (M4 local) */
#define LPC43XX_SRAM0_SIZE      (32 * 1024)

#define LPC43XX_SRAM1_BASE      0x10080000UL   /* 40 kB */
#define LPC43XX_SRAM1_SIZE      (40 * 1024)

#define LPC43XX_SRAM2_BASE      0x20000000UL   /* 32 kB */
#define LPC43XX_SRAM2_SIZE      (32 * 1024)

#define LPC43XX_SRAM3_BASE      0x20008000UL   /* 16 kB (M0 local) */
#define LPC43XX_SRAM3_SIZE      (16 * 1024)

/* External SPIFI flash window */
#define LPC43XX_SPIFI_BASE      0x14000000UL
#define LPC43XX_SPIFI_SIZE      (16 * 1024 * 1024)  /* 16 MB window */

/* AHB peripheral bus 0 (0x40000000) */
#define LPC43XX_SCT_BASE        0x40000000UL
#define LPC43XX_GPDMA_BASE      0x40002000UL
#define LPC43XX_SPIFI_CTRL_BASE 0x40003000UL
#define LPC43XX_SDMMC_BASE      0x40004000UL
#define LPC43XX_EMC_BASE        0x40005000UL
#define LPC43XX_USB0_BASE       0x40006000UL
#define LPC43XX_USB1_BASE       0x40007000UL
#define LPC43XX_ETHERNET_BASE   0x40010000UL
#define LPC43XX_LCD_BASE        0x40008000UL

/* AHB peripheral bus 1 (0x400A0000) */
#define LPC43XX_SCU_BASE        0x40086000UL

/* AHB peripheral bus 2 (APB0/1 – 0x400x0000) */
#define LPC43XX_WWDT_BASE       0x40080000UL
#define LPC43XX_USART0_BASE     0x40081000UL
#define LPC43XX_UART1_BASE      0x40082000UL
#define LPC43XX_USART2_BASE     0x400C1000UL
#define LPC43XX_USART3_BASE     0x400C2000UL
#define LPC43XX_TIMER0_BASE     0x40084000UL
#define LPC43XX_TIMER1_BASE     0x40085000UL
#define LPC43XX_TIMER2_BASE     0x400C3000UL
#define LPC43XX_TIMER3_BASE     0x400C4000UL
#define LPC43XX_SSP0_BASE       0x40083000UL
#define LPC43XX_SSP1_BASE       0x400C5000UL
#define LPC43XX_I2C0_BASE       0x400A1000UL
#define LPC43XX_I2C1_BASE       0x400E0000UL
#define LPC43XX_I2S0_BASE       0x400A2000UL
#define LPC43XX_I2S1_BASE       0x400A3000UL
#define LPC43XX_ADC0_BASE       0x400E3000UL
#define LPC43XX_ADC1_BASE       0x400E4000UL
#define LPC43XX_DAC_BASE        0x400E1000UL
#define LPC43XX_GPIO_BASE       0x400F4000UL
#define LPC43XX_SPI_BASE        0x400E5000UL
#define LPC43XX_SGPIO_BASE      0x40101000UL

/* Clock / Reset / Power management */
#define LPC43XX_CGU_BASE        0x40050000UL
#define LPC43XX_CCU1_BASE       0x40051000UL
#define LPC43XX_CCU2_BASE       0x40052000UL
#define LPC43XX_CREG_BASE       0x40043000UL
#define LPC43XX_RGU_BASE        0x40053000UL
#define LPC43XX_PMC_BASE        0x40042000UL

/* -----------------------------------------------------------------------
 * IRQ numbers  (UM10503 Table 80 – NVIC positions for M4 core)
 * ----------------------------------------------------------------------- */
#define LPC43XX_IRQ_DAC         0
#define LPC43XX_IRQ_M0CORE      1
#define LPC43XX_IRQ_DMA         2
#define LPC43XX_IRQ_FLASHEEPROMDMA 3
#define LPC43XX_IRQ_ETH         5
#define LPC43XX_IRQ_SDMMC       6
#define LPC43XX_IRQ_LCD         7
#define LPC43XX_IRQ_USB0        8
#define LPC43XX_IRQ_USB1        9
#define LPC43XX_IRQ_SCT         9
#define LPC43XX_IRQ_RITIMER     10
#define LPC43XX_IRQ_TIMER0      12
#define LPC43XX_IRQ_TIMER1      13
#define LPC43XX_IRQ_TIMER2      14
#define LPC43XX_IRQ_TIMER3      15
#define LPC43XX_IRQ_MCPWM       16
#define LPC43XX_IRQ_ADC0        17
#define LPC43XX_IRQ_I2C0        18
#define LPC43XX_IRQ_I2C1        19
#define LPC43XX_IRQ_SPI         20
#define LPC43XX_IRQ_ADC1        21
#define LPC43XX_IRQ_SSP0        22
#define LPC43XX_IRQ_SSP1        23
#define LPC43XX_IRQ_USART0      24
#define LPC43XX_IRQ_UART1       25
#define LPC43XX_IRQ_USART2      26
#define LPC43XX_IRQ_USART3      27
#define LPC43XX_IRQ_I2S0        28
#define LPC43XX_IRQ_I2S1        29
#define LPC43XX_IRQ_SPIFI       30
#define LPC43XX_IRQ_SGPIO       31
#define LPC43XX_IRQ_GPIO0       32
#define LPC43XX_IRQ_GPIO1       33
#define LPC43XX_IRQ_GPIO2       34
#define LPC43XX_IRQ_GPIO3       35
#define LPC43XX_IRQ_GPIO4       36
#define LPC43XX_IRQ_GPIO5       37
#define LPC43XX_IRQ_GPIO6       38
#define LPC43XX_IRQ_GPIO7       39
#define LPC43XX_IRQ_GINT0       40
#define LPC43XX_IRQ_GINT1       41
#define LPC43XX_IRQ_EVRT        42
#define LPC43XX_IRQ_CAN1        43
#define LPC43XX_IRQ_VADC        44
#define LPC43XX_IRQ_ATIMER      46
#define LPC43XX_IRQ_RTC         47
#define LPC43XX_IRQ_WDT         49
#define LPC43XX_IRQ_M0SUB       50
#define LPC43XX_IRQ_CAN0        51
#define LPC43XX_IRQ_QEI         52
#define LPC43XX_NUM_IRQ         53

/* -----------------------------------------------------------------------
 * QOM type names
 * ----------------------------------------------------------------------- */
#define TYPE_LPC43XX_SOC        "lpc43xx-soc"

OBJECT_DECLARE_SIMPLE_TYPE(LPC43XXState, LPC43XX_SOC)

/* -----------------------------------------------------------------------
 * Machine type names
 * ----------------------------------------------------------------------- */
#define TYPE_LPC43XX_MACHINE          "lpc43xx"
#define TYPE_LPC4350_MACHINE          MACHINE_TYPE_NAME("lpc4350")
#define TYPE_LPC4330_MACHINE          MACHINE_TYPE_NAME("lpc4330")
#define TYPE_LPC4320_MACHINE          MACHINE_TYPE_NAME("lpc4320")
#define TYPE_LPC4310_MACHINE          MACHINE_TYPE_NAME("lpc4310")

/* HackRF / PortaPack target */
#define TYPE_HACKRF_PORTAPACK_MACHINE MACHINE_TYPE_NAME("hackrf-portapack")

/* -----------------------------------------------------------------------
 * SoC state structure
 * ----------------------------------------------------------------------- */
struct LPC43XXState {
    SysBusDevice parent_obj;

    /* Primary Cortex-M4 core */
    ARMv7MState  m4;

    /* Secondary Cortex-M0 core (LPC4350/30/20 only) */
    ARMv7MState  m0;
    bool         has_m0;

    /* SRAM regions */
    MemoryRegion sram0;
    MemoryRegion sram1;
    MemoryRegion sram2;
    MemoryRegion sram3;

    /* Boot ROM */
    MemoryRegion boot_rom;

    /* SPIFI flash window (guest-visible, backed by RAM for emulation) */
    MemoryRegion spifi_mem;

    /* Clocks */
    Clock *sysclk;   /* M4 core clock  – up to 204 MHz */
    Clock *refclk;   /* Reference / sys-tick clock 1 MHz */
    Clock *irc;      /* Internal RC 12 MHz */

    /* Variant */
    LPC43XXVariant variant;
};

#endif /* HW_ARM_LPC43XX_H */
