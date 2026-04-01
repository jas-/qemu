/*
 * NXP LPC43xx Clock Control Units (CCU1 / CCU2) emulation
 *
 * UM10503 §13 – Clock Control Unit
 *
 * Copyright (c) 2024 QEMU Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_MISC_LPC43XX_CCU_H
#define HW_MISC_LPC43XX_CCU_H

#include "hw/core/sysbus.h"
#include "qom/object.h"

#define TYPE_LPC43XX_CCU "lpc43xx-ccu"
OBJECT_DECLARE_SIMPLE_TYPE(LPC43XXCCUState, LPC43XX_CCU)

/* Each CCU has a 0x1000 byte register window with pairs of
 * (CFG, STAT) for every branch clock.  We model the full
 * 0x1000 window as a register array.                        */
#define CCU_REG_WINDOW   0x1000
#define CCU_NUM_REGS     (CCU_REG_WINDOW / 4)

/* Common CFG register bits */
#define CCU_CFG_RUN      (1u << 0)
#define CCU_CFG_AUTO     (1u << 1)
#define CCU_CFG_WAKEUP   (1u << 2)
/* STAT register bits */
#define CCU_STAT_RUN     (1u << 0)
#define CCU_STAT_AUTO    (1u << 1)
#define CCU_STAT_WAKEUP  (1u << 2)

/* Notable CCU1 branch offsets */
#define CCU1_CLK_APB3_BUS    0x100
#define CCU1_CLK_APB1_BUS    0x200
#define CCU1_CLK_M4_BUS      0x300
#define CCU1_CLK_M4_CORE     0x308  /* CFG, STAT pairs */
#define CCU1_CLK_M4_CREG     0x310

struct LPC43XXCCUState {
    SysBusDevice parent_obj;
    MemoryRegion  mmio;
    uint32_t      regs[CCU_NUM_REGS];
    uint32_t      index;   /* 1 or 2 – distinguishes CCU1 / CCU2 */
};

#endif /* HW_MISC_LPC43XX_CCU_H */
