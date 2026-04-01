/*
 * NXP LPC43xx Clock Control Unit (CCU1 / CCU2) emulation
 *
 * The CCU contains branch clock registers, each a (CFG, STAT) 8-byte pair.
 * Firmware writes CFG.RUN=1 to enable a branch; we reflect RUN→STAT.RUN
 * immediately since there is no clock propagation delay to model.
 *
 * Copyright (c) 2024 QEMU Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Reference: NXP UM10503 Chapter 13
 */

#include "qemu/osdep.h"
#include "hw/core/sysbus.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/core/qdev-properties.h"
#include "migration/vmstate.h"

#include "hw/core/resettable.h"

#include "hw/misc/lpc43xx_ccu.h"

/* -----------------------------------------------------------------------
 * Read
 * ----------------------------------------------------------------------- */
static uint64_t lpc43xx_ccu_read(void *opaque, hwaddr offset, unsigned size)
{
    LPC43XXCCUState *s = LPC43XX_CCU(opaque);

    if (offset >= CCU_REG_WINDOW) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "lpc43xx-ccu%u: read 0x%" HWADDR_PRIx
                      " out of range\n", s->index, offset);
        return 0;
    }
    return s->regs[offset / 4];
}

/* -----------------------------------------------------------------------
 * Write – update CFG and mirror relevant bits to STAT immediately
 * ----------------------------------------------------------------------- */
static void lpc43xx_ccu_write(void *opaque, hwaddr offset,
                              uint64_t val, unsigned size)
{
    LPC43XXCCUState *s = LPC43XX_CCU(opaque);
    unsigned idx;

    if (offset >= CCU_REG_WINDOW) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "lpc43xx-ccu%u: write 0x%" HWADDR_PRIx
                      " out of range\n", s->index, offset);
        return;
    }

    idx = offset / 4;

    /*
     * The CCU layout interleaves CFG (even word) and STAT (odd word) for
     * each branch.  Identify CFG writes and update the corresponding STAT.
     */
    if ((offset & 4) == 0) {
        /* CFG register */
        s->regs[idx] = (uint32_t)val;
        /* Mirror RUN, AUTO, WAKEUP bits into STAT (next word) */
        s->regs[idx + 1] = (uint32_t)val & (CCU_STAT_RUN |
                                             CCU_STAT_AUTO |
                                             CCU_STAT_WAKEUP);
    } else {
        /* STAT registers are read-only */
        qemu_log_mask(LOG_GUEST_ERROR,
                      "lpc43xx-ccu%u: write to read-only STAT @ "
                      "0x%" HWADDR_PRIx "\n", s->index, offset);
    }
}

static const MemoryRegionOps lpc43xx_ccu_ops = {
    .read  = lpc43xx_ccu_read,
    .write = lpc43xx_ccu_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

/* -----------------------------------------------------------------------
 * Reset – bring all branches to "running" as the hardware does after a
 * full-chip reset with the IRC as clock source.
 * ----------------------------------------------------------------------- */
//static void lpc43xx_ccu_reset(DeviceState *dev)
static void lpc43xx_ccu_reset(Object *obj, ResetType type)

{
    LPC43XXCCUState *s = LPC43XX_CCU(obj);
    unsigned i;

    memset(s->regs, 0, sizeof(s->regs));

    /*
     * After reset several branch clocks are automatically enabled by the
     * hardware (RUN=1, AUTO=1).  We set all CFG/STAT pairs to RUN to avoid
     * firmware stalling while waiting for clock status.
     */
    for (i = 0; i < CCU_NUM_REGS; i += 2) {
        s->regs[i]     = CCU_CFG_RUN | CCU_CFG_AUTO;   /* CFG  */
        s->regs[i + 1] = CCU_STAT_RUN | CCU_STAT_AUTO; /* STAT */
    }
}

/* -----------------------------------------------------------------------
 * Realize
 * ----------------------------------------------------------------------- */
static void lpc43xx_ccu_realize(DeviceState *dev, Error **errp)
{
    LPC43XXCCUState *s = LPC43XX_CCU(dev);
    char name[32];

    snprintf(name, sizeof(name), "lpc43xx-ccu%u", s->index);
    memory_region_init_io(&s->mmio, OBJECT(dev), &lpc43xx_ccu_ops, s,
                          name, CCU_REG_WINDOW);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->mmio);
}

/* -----------------------------------------------------------------------
 * Properties / VMState
 * ----------------------------------------------------------------------- */
/*
 static Property lpc43xx_ccu_props[] = {
    DEFINE_PROP_UINT32("index", LPC43XXCCUState, index, 1),
    DEFINE_PROP_END_OF_LIST()
};
*/


static const VMStateDescription vmstate_lpc43xx_ccu = {
    .name = "lpc43xx-ccu",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, LPC43XXCCUState, CCU_NUM_REGS),
        VMSTATE_END_OF_LIST()
    },
};


/* -----------------------------------------------------------------------
 * Class / type
 * ----------------------------------------------------------------------- */
static void lpc43xx_ccu_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    dc->realize  = lpc43xx_ccu_realize;
    //dc->reset    = lpc43xx_ccu_reset;
    rc->phases.hold = lpc43xx_ccu_reset;
    dc->vmsd     = &vmstate_lpc43xx_ccu;
    dc->desc     = "LPC43xx Clock Control Unit";
    //dc->props_    = lpc43xx_ccu_props;
    //device_class_set_props(dc, lpc43xx_ccu_props);
}

static const TypeInfo lpc43xx_ccu_info = {
    .name          = TYPE_LPC43XX_CCU,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(LPC43XXCCUState),
    .class_init    = lpc43xx_ccu_class_init,
};

static void lpc43xx_ccu_register(void)
{
    type_register_static(&lpc43xx_ccu_info);
}
type_init(lpc43xx_ccu_register)
