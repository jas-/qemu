/*
 * NXP LPC43xx Configuration Registers (CREG) emulation
 *
 * CREG provides:
 *   - Chip ID register (read-only, variant-dependent)
 *   - CREG0: 32 kHz / 1 kHz oscillator enables, USB PHY power
 *   - CREG6: ETB/QSPI multiplexer bits
 *   - M4MEMMAP: relocatable M4 shadow map register
 *   - M0 inter-core event / reset registers
 *
 * Copyright (c) 2024 QEMU Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Reference: NXP UM10503 Chapter 15
 */

#include "qemu/osdep.h"
#include "hw/core/sysbus.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/core/qdev-properties.h"
#include "migration/vmstate.h"

#include "hw/core/resettable.h"

#include "hw/misc/lpc43xx_scu_creg.h"

/* -----------------------------------------------------------------------
 * Read
 * ----------------------------------------------------------------------- */
static uint64_t lpc43xx_creg_read(void *opaque, hwaddr offset, unsigned size)
{
    LPC43XXCREGState *s = LPC43XX_CREG(opaque);

    if (offset >= CREG_REG_WINDOW) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "lpc43xx-creg: read 0x%" HWADDR_PRIx
                      " out of range\n", offset);
        return 0;
    }

    /* CHIPID is always the variant-specific value */
    if (offset == CREG_CHIPID) {
        return s->chip_id;
    }

    return s->regs[offset / 4];
}

/* -----------------------------------------------------------------------
 * Write
 * ----------------------------------------------------------------------- */
static void lpc43xx_creg_write(void *opaque, hwaddr offset,
                               uint64_t val, unsigned size)
{
    LPC43XXCREGState *s = LPC43XX_CREG(opaque);

    if (offset >= CREG_REG_WINDOW) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "lpc43xx-creg: write 0x%" HWADDR_PRIx
                      " out of range\n", offset);
        return;
    }

    /* CHIPID is read-only */
    if (offset == CREG_CHIPID) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "lpc43xx-creg: write to read-only CHIPID\n");
        return;
    }

    s->regs[offset / 4] = (uint32_t)val;
}

static const MemoryRegionOps lpc43xx_creg_ops = {
    .read  = lpc43xx_creg_read,
    .write = lpc43xx_creg_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

/* -----------------------------------------------------------------------
 * Reset
 * ----------------------------------------------------------------------- */
//static void lpc43xx_creg_reset(DeviceState *dev)
static void lpc43xx_creg_reset(Object *obj, ResetType type)

{
    LPC43XXCREGState *s = LPC43XX_CREG(obj);

    memset(s->regs, 0, sizeof(s->regs));

    /*
     * CREG0 reset value: 1 kHz and 32 kHz oscillators disabled,
     * USB0 PHY in power-down.
     */
    s->regs[CREG_CREG0 / 4] = 0x00000010; /* WAKEUP0CTRL=1 per reset table */

    /*
     * FLASHCFGA/B reset value: 9 wait-states (safe for 204 MHz).
     * Firmware typically lowers this after clocking.
     */
    s->regs[CREG_FLASHCFGA / 4] = 0x0003A000;
    s->regs[CREG_FLASHCFGB / 4] = 0x0003A000;

    /* M4MEMMAP default: 0x00000000 (maps to start of SRAM0/SPIFI area) */
    s->regs[CREG_M4MEMMAP / 4] = 0x00000000;
}

/* -----------------------------------------------------------------------
 * Realize
 * ----------------------------------------------------------------------- */
static void lpc43xx_creg_realize(DeviceState *dev, Error **errp)
{
    LPC43XXCREGState *s = LPC43XX_CREG(dev);

    memory_region_init_io(&s->mmio, OBJECT(dev), &lpc43xx_creg_ops, s,
                          "lpc43xx-creg", CREG_REG_WINDOW);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->mmio);
}

/* -----------------------------------------------------------------------
 * Properties
 * ----------------------------------------------------------------------- */
/*
 static Property lpc43xx_creg_props[] = {
    DEFINE_PROP_UINT32("chip-id", LPC43XXCREGState, chip_id, LPC4350_CHIP_ID),
    DEFINE_PROP_END_OF_LIST(),
};
*/

/* -----------------------------------------------------------------------
 * VMState
 * ----------------------------------------------------------------------- */
/*
 static const VMStateDescription vmstate_lpc43xx_creg = {
    .name = "lpc43xx-creg",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, LPC43XXCREGState, CREG_NUM_REGS),
        VMSTATE_END_OF_LIST()
    },
};
*/
/* -----------------------------------------------------------------------
 * Class / type
 * ----------------------------------------------------------------------- */
static void lpc43xx_creg_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    dc->realize = lpc43xx_creg_realize;
    //dc->reset   = lpc43xx_creg_reset;
    rc->phases.hold = lpc43xx_creg_reset;
    //dc->vmsd    = &vmstate_lpc43xx_creg;
    dc->desc    = "LPC43xx Configuration Registers";
    //device_class_set_props(dc, lpc43xx_creg_props);
}

static const TypeInfo lpc43xx_creg_info = {
    .name          = TYPE_LPC43XX_CREG,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(LPC43XXCREGState),
    .class_init    = lpc43xx_creg_class_init,
};

static void lpc43xx_creg_register(void)
{
    type_register_static(&lpc43xx_creg_info);
}
type_init(lpc43xx_creg_register)
