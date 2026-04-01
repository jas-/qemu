/*
 * NXP LPC43xx Clock Generation Unit (CGU) emulation
 *
 * UM10503 §12 – Clock Generation Unit
 *
 * Copyright (c) 2024 QEMU Contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_MISC_LPC43XX_CGU_H
#define HW_MISC_LPC43XX_CGU_H

#include "hw/core/sysbus.h"
#include "qom/object.h"

#define TYPE_LPC43XX_CGU "lpc43xx-cgu"
OBJECT_DECLARE_SIMPLE_TYPE(LPC43XXCGUState, LPC43XX_CGU)

/* CGU register offsets (UM10503 Table 167) */
#define CGU_FREQ_MON        0x014
#define CGU_XTAL_OSC_CTRL   0x018
#define CGU_PLL0USB_STAT    0x01C
#define CGU_PLL0USB_CTRL    0x020
#define CGU_PLL0USB_MDIV    0x024
#define CGU_PLL0USB_NP_DIV  0x028
#define CGU_PLL0AUDIO_STAT  0x02C
#define CGU_PLL0AUDIO_CTRL  0x030
#define CGU_PLL0AUDIO_MDIV  0x034
#define CGU_PLL0AUDIO_NP    0x038
#define CGU_PLL0AUDIO_FRAC  0x03C
#define CGU_PLL1_STAT       0x040
#define CGU_PLL1_CTRL       0x044
#define CGU_IDIVS_BASE      0x048  /* IDIVA..IDIVE: 0x048,04C,050,054,058 */
#define CGU_BASE_SAFE_CLK   0x05C
#define CGU_BASE_USB0_CLK   0x060
#define CGU_BASE_PERIPH_CLK 0x064
#define CGU_BASE_USB1_CLK   0x068
#define CGU_BASE_M4_CLK     0x06C   /* ← primary M4 base clock */
#define CGU_BASE_SPIFI_CLK  0x070
#define CGU_BASE_SPI_CLK    0x074
#define CGU_BASE_PHY_RX_CLK 0x078
#define CGU_BASE_PHY_TX_CLK 0x07C
#define CGU_BASE_APB1_CLK   0x080
#define CGU_BASE_APB3_CLK   0x084
#define CGU_BASE_LCD_CLK    0x088
#define CGU_BASE_ADCHS_CLK  0x08C
#define CGU_BASE_SDIO_CLK   0x090
#define CGU_BASE_SSP0_CLK   0x094
#define CGU_BASE_SSP1_CLK   0x098
#define CGU_BASE_UART0_CLK  0x09C
#define CGU_BASE_UART1_CLK  0x0A0
#define CGU_BASE_UART2_CLK  0x0A4
#define CGU_BASE_UART3_CLK  0x0A8
#define CGU_BASE_OUT0_CLK   0x0AC
#define CGU_BASE_OUT1_CLK   0x0C0
#define CGU_REG_SIZE        0x0C4

/* PLL1_CTRL bits */
#define PLL1_CTRL_PD        (1u << 0)   /* power-down */
#define PLL1_CTRL_BYPASS    (1u << 1)
#define PLL1_CTRL_FBSEL     (1u << 6)
#define PLL1_CTRL_DIRECT    (1u << 7)
#define PLL1_CTRL_PSEL_SHIFT 8
#define PLL1_CTRL_PSEL_MASK  0x3
#define PLL1_CTRL_NSEL_SHIFT 12
#define PLL1_CTRL_NSEL_MASK  0x3
#define PLL1_CTRL_MSEL_SHIFT 16
#define PLL1_CTRL_MSEL_MASK  0xFF
#define PLL1_STAT_LOCK      (1u << 0)

/* BASE_CLK source selectors */
#define BASE_CLK_SEL_SHIFT  24
#define BASE_CLK_SEL_MASK   0x1F
#define BASE_CLK_PD         (1u << 0)

struct LPC43XXCGUState {
    SysBusDevice parent_obj;
    MemoryRegion  mmio;
    uint32_t      regs[CGU_REG_SIZE / 4];
};

#endif /* HW_MISC_LPC43XX_CGU_H */
