/*
 * NXP LPC43xx System Control Unit (SCU) – pin-mux / pad control emulation
 *
 * The SCU controls the function of each physical pin.  For emulation we
 * simply provide a read/write register file; the actual pin-mux state has
 * no effect since QEMU devices use their own IRQ lines and memory maps.
 *
 * Copyright (c) 2024 QEMU Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Reference: NXP UM10503 Chapter 17
 */

#include "qemu/osdep.h"
#include "hw/core/sysbus.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "migration/vmstate.h"

#include "hw/core/resettable.h"

#include "hw/misc/lpc43xx_scu_creg.h"

/* -----------------------------------------------------------------------
 * Read / Write – simple register file
 * ----------------------------------------------------------------------- */
static uint64_t lpc43xx_scu_read(void *opaque, hwaddr offset, unsigned size)
{
    LPC43XXSCUState *s = LPC43XX_SCU(opaque);

    if (offset >= SCU_REG_WINDOW) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "lpc43xx-scu: read 0x%" HWADDR_PRIx
                      " out of range\n", offset);
        return 0;
    }
    return s->regs[offset / 4];
}

static void lpc43xx_scu_write(void *opaque, hwaddr offset,
                              uint64_t val, unsigned size)
{
    LPC43XXSCUState *s = LPC43XX_SCU(opaque);

    if (offset >= SCU_REG_WINDOW) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "lpc43xx-scu: write 0x%" HWADDR_PRIx
                      " out of range\n", offset);
        return;
    }
    s->regs[offset / 4] = (uint32_t)val;
}

static const MemoryRegionOps lpc43xx_scu_ops = {
    .read  = lpc43xx_scu_read,
    .write = lpc43xx_scu_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

/* -----------------------------------------------------------------------
 * Reset – default SFS values: pull-up enabled, input buffer disabled
 * ----------------------------------------------------------------------- */
 //static void lpc43xx_scu_reset(DeviceState *dev)
static void lpc43xx_scu_reset(Object *obj, ResetType type)
 {
    LPC43XXSCUState *s = LPC43XX_SCU(obj);
    unsigned i;

    for (i = 0; i < SCU_NUM_REGS; i++) {
        /* Default: function=0, pull-down disabled (EPD=0), pull-up enabled
         * (EPUN=0), EHS=0, EZI=0, ZIF=0 */
        s->regs[i] = 0x00000000;
    }
}

/* -----------------------------------------------------------------------
 * Realize
 * ----------------------------------------------------------------------- */
static void lpc43xx_scu_realize(DeviceState *dev, Error **errp)
{
    LPC43XXSCUState *s = LPC43XX_SCU(dev);

    memory_region_init_io(&s->mmio, OBJECT(dev), &lpc43xx_scu_ops, s,
                          "lpc43xx-scu", SCU_REG_WINDOW);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->mmio);
}

/* -----------------------------------------------------------------------
 * VMState
 * ----------------------------------------------------------------------- */
static const VMStateDescription vmstate_lpc43xx_scu = {
    .name = "lpc43xx-scu",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, LPC43XXSCUState, SCU_NUM_REGS),
        VMSTATE_END_OF_LIST()
    },
};

/* -----------------------------------------------------------------------
 * Class / type
 * ----------------------------------------------------------------------- */
static void lpc43xx_scu_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    dc->realize = lpc43xx_scu_realize;
    //dc->reset   = lpc43xx_scu_reset;
    rc->phases.hold = lpc43xx_scu_reset;
    dc->vmsd    = &vmstate_lpc43xx_scu;
    dc->desc    = "LPC43xx System Control Unit (pin-mux)";
}

static const TypeInfo lpc43xx_scu_info = {
    .name          = TYPE_LPC43XX_SCU,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(LPC43XXSCUState),
    .class_init    = lpc43xx_scu_class_init,
};

static void lpc43xx_scu_register(void)
{
    type_register_static(&lpc43xx_scu_info);
}
type_init(lpc43xx_scu_register)
