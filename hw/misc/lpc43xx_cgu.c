/*
 * NXP LPC43xx Clock Generation Unit (CGU) emulation
 *
 * The CGU contains two integer PLLs (PLL0 USB, PLL0 Audio) and one
 * fractional PLL (PLL1 – the main M4/peripheral PLL), plus 5 integer
 * dividers (IDIVA–IDIVE) and a set of BASE_CLK mux registers that route
 * clock sources to each peripheral domain.
 *
 * For emulation purposes the key behaviour required is:
 *   • PLL1 STAT.LOCK is always reported as 1 (locked) when not
 *     powered-down, so firmware boot loops waiting for lock proceed.
 *   • BASE_M4_CLK and all other BASE registers accept writes and
 *     reflect them back correctly so firmware clock queries work.
 *
 * Copyright (c) 2024 QEMU Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Reference: NXP UM10503 Chapter 12
 */

#include "qemu/osdep.h"
#include "hw/core/sysbus.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/core/qdev-properties.h"
#include "migration/vmstate.h"

#include "hw/core/resettable.h"

#include "hw/misc/lpc43xx_cgu.h"

/* -----------------------------------------------------------------------
 * Read
 * ----------------------------------------------------------------------- */
static uint64_t lpc43xx_cgu_read(void *opaque, hwaddr offset, unsigned size)
{
    LPC43XXCGUState *s = LPC43XX_CGU(opaque);
    uint32_t val;

    if (offset >= CGU_REG_SIZE) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "lpc43xx-cgu: read at 0x%" HWADDR_PRIx
                      " out of range\n", offset);
        return 0;
    }

    val = s->regs[offset / 4];

    /* PLL1 STAT – report locked if not powered-down */
    if (offset == CGU_PLL1_STAT) {
        if (!(s->regs[CGU_PLL1_CTRL / 4] & PLL1_CTRL_PD)) {
            val |= PLL1_STAT_LOCK;
        } else {
            val &= ~PLL1_STAT_LOCK;
        }
    }

    /* PLL0USB_STAT – always locked */
    if (offset == CGU_PLL0USB_STAT) {
        val |= PLL1_STAT_LOCK;
    }

    /* PLL0AUDIO_STAT – always locked */
    if (offset == CGU_PLL0AUDIO_STAT) {
        val |= PLL1_STAT_LOCK;
    }

    return val;
}

/* -----------------------------------------------------------------------
 * Write
 * ----------------------------------------------------------------------- */
static void lpc43xx_cgu_write(void *opaque, hwaddr offset,
                              uint64_t val, unsigned size)
{
    LPC43XXCGUState *s = LPC43XX_CGU(opaque);

    if (offset >= CGU_REG_SIZE) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "lpc43xx-cgu: write at 0x%" HWADDR_PRIx
                      " out of range\n", offset);
        return;
    }

    /* STAT registers are read-only */
    switch (offset) {
    case CGU_PLL1_STAT:
    case CGU_PLL0USB_STAT:
    case CGU_PLL0AUDIO_STAT:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "lpc43xx-cgu: write to read-only STAT reg "
                      "@ 0x%" HWADDR_PRIx "\n", offset);
        return;
    default:
        break;
    }

    s->regs[offset / 4] = (uint32_t)val;
}

static const MemoryRegionOps lpc43xx_cgu_ops = {
    .read  = lpc43xx_cgu_read,
    .write = lpc43xx_cgu_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

/* -----------------------------------------------------------------------
 * Reset
 * ----------------------------------------------------------------------- */
//static void lpc43xx_cgu_reset(DeviceState *dev)
static void lpc43xx_cgu_reset(Object *obj, ResetType type)
{
    LPC43XXCGUState *s = LPC43XX_CGU(obj);

    memset(s->regs, 0, sizeof(s->regs));

    /*
     * After reset the chip is clocked by the internal RC oscillator (IRC)
     * at 12 MHz.  BASE_M4_CLK reset value selects IRC (source = 0x01).
     * PLL1 defaults to powered-down.
     */
    s->regs[CGU_PLL1_CTRL / 4]   = PLL1_CTRL_PD;   /* PD=1, locked=0 */
    s->regs[CGU_BASE_M4_CLK / 4] = (0x01 << BASE_CLK_SEL_SHIFT); /* IRC */

    /* XTAL oscillator disabled by default */
    s->regs[CGU_XTAL_OSC_CTRL / 4] = 0x00000001; /* bypass=0, enable=1 */

    /* Set PLL0 audio/USB as powered-down */
    s->regs[CGU_PLL0USB_CTRL / 4]   = PLL1_CTRL_PD;
    s->regs[CGU_PLL0AUDIO_CTRL / 4] = PLL1_CTRL_PD;
}

/* -----------------------------------------------------------------------
 * Realize
 * ----------------------------------------------------------------------- */
static void lpc43xx_cgu_realize(DeviceState *dev, Error **errp)
{
    LPC43XXCGUState *s = LPC43XX_CGU(dev);

    memory_region_init_io(&s->mmio, OBJECT(dev), &lpc43xx_cgu_ops, s,
                          "lpc43xx-cgu", CGU_REG_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->mmio);
}

/* -----------------------------------------------------------------------
 * VMState
 * ----------------------------------------------------------------------- */
static const VMStateDescription vmstate_lpc43xx_cgu = {
    .name = "lpc43xx-cgu",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, LPC43XXCGUState,
                             CGU_REG_SIZE / 4),
        VMSTATE_END_OF_LIST()
    },
};

/* -----------------------------------------------------------------------
 * Class / type
 * ----------------------------------------------------------------------- */
static void lpc43xx_cgu_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    dc->realize    = lpc43xx_cgu_realize;
    //dc->reset      = lpc43xx_cgu_reset;
    rc->phases.hold = lpc43xx_cgu_reset;
    dc->vmsd       = &vmstate_lpc43xx_cgu;
    dc->desc       = "LPC43xx Clock Generation Unit";
}

static const TypeInfo lpc43xx_cgu_info = {
    .name          = TYPE_LPC43XX_CGU,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(LPC43XXCGUState),
    .class_init    = lpc43xx_cgu_class_init,
};

static void lpc43xx_cgu_register(void)
{
    type_register_static(&lpc43xx_cgu_info);
}
type_init(lpc43xx_cgu_register)
