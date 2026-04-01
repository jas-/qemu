/*
 * NXP LPC43xx System Control Unit (SCU) – pin-mux / pad control
 *
 * UM10503 §17
 *
 * Copyright (c) 2024 QEMU Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_MISC_LPC43XX_SCU_H
#define HW_MISC_LPC43XX_SCU_H

#include "hw/core/sysbus.h"
#include "qom/object.h"

#define TYPE_LPC43XX_SCU "lpc43xx-scu"
OBJECT_DECLARE_SIMPLE_TYPE(LPC43XXSCUState, LPC43XX_SCU)

/* The SCU has 160+ SFS (pin-function-select) registers.
 * We allocate a 0x1000-byte window.                     */
#define SCU_REG_WINDOW  0x1000
#define SCU_NUM_REGS    (SCU_REG_WINDOW / 4)

/* SFS register bits */
#define SCU_SFS_MODE_MASK   0x7
#define SCU_SFS_EPD         (1u << 3)   /* pull-down enable */
#define SCU_SFS_EPUN        (1u << 4)   /* pull-up disable  */
#define SCU_SFS_EHS         (1u << 5)   /* slew rate        */
#define SCU_SFS_EZI         (1u << 6)   /* input buffer     */
#define SCU_SFS_ZIF         (1u << 7)   /* input filter     */

struct LPC43XXSCUState {
    SysBusDevice parent_obj;
    MemoryRegion  mmio;
    uint32_t      regs[SCU_NUM_REGS];
};

#endif /* HW_MISC_LPC43XX_SCU_H */


/*
 * NXP LPC43xx Configuration Registers (CREG)
 *
 * UM10503 §15
 *
 * Copyright (c) 2024 QEMU Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_MISC_LPC43XX_CREG_H
#define HW_MISC_LPC43XX_CREG_H

#include "hw/core/sysbus.h"
#include "qom/object.h"

#define TYPE_LPC43XX_CREG "lpc43xx-creg"
OBJECT_DECLARE_SIMPLE_TYPE(LPC43XXCREGState, LPC43XX_CREG)

/* CREG register offsets */
#define CREG_CREG0          0x004   /* M0SUB reset / Flash wait-states */
#define CREG_PMUCON         0x008
#define CREG_CHIPID         0x200   /* read-only chip ID */
#define CREG_FLASHCFGA      0x204
#define CREG_FLASHCFGB      0x208
#define CREG_ETBCFG         0x20C
#define CREG_CREG6          0x210   /* QSPI / ETB selection */
#define CREG_M4MEMMAP       0x218   /* M4 shadow-map base address */
#define CREG_M0APPTXEVENT   0x400
#define CREG_M0SUBTXEVENT   0x404

/* CREG0 bits */
#define CREG0_EN1KHZ        (1u << 0)
#define CREG0_EN32KHZ       (1u << 1)
#define CREG0_RESET32KHZ    (1u << 2)
#define CREG0_PD32KHZ       (1u << 3)
#define CREG0_USB0PHY       (1u << 5)
#define CREG0_ALARMCTRL_SHIFT 6
#define CREG0_BODLVL1_SHIFT 8
#define CREG0_BODLVL2_SHIFT 10
#define CREG0_SAMPLECTRL_SHIFT 12
#define CREG0_WAKEUP0CTRL_SHIFT 14
#define CREG0_WAKEUP1CTRL_SHIFT 16

/* Chip IDs (CREG_CHIPID) – UM10503 Table 981 */
#define LPC4350_CHIP_ID     0xA001C830
#define LPC4330_CHIP_ID     0xA001C730
#define LPC4320_CHIP_ID     0xA001C620
#define LPC4310_CHIP_ID     0xA001C510

#define CREG_REG_WINDOW     0x500
#define CREG_NUM_REGS       (CREG_REG_WINDOW / 4)

struct LPC43XXCREGState {
    SysBusDevice parent_obj;
    MemoryRegion  mmio;
    uint32_t      regs[CREG_NUM_REGS];
    uint32_t      chip_id;   /* set by machine based on variant */
};

#endif /* HW_MISC_LPC43XX_CREG_H */
