// SPDX-License-Identifier: BSD-2-Clause-Patent
// MediaTek MT6991 Dimensity 9400+ UFS 4.1 Host Controller facts.
#ifndef NATIVE_UFS_GCC_UFS_H
#define NATIVE_UFS_GCC_UFS_H

// MT6991 UFS 4.1 controller (ufshci@16810000, 0x2a00 bytes)
#define UFS_HC_BASE                   0x16810000ULL
#define UFS_HC_SIZE                   0x2a00ULL

// LUN 2 (sdc) holds the primary user storage and partitions on klimt
#define UFS_TARGET_LUN                2

#define GCC_BASE                      0x00100000ULL
#define GCC_UFS_PHY_GDSCR             0x77004
#define GCC_UFS_MEM_PHY_GDSCR         0x9e000
#define GCC_UFS_PHY_AXI_ENABLE        0x77018
#define GCC_UFS_PHY_AHB_ENABLE        0x77024
#define GCC_UFS_PHY_TX_SYMBOL0_ENABLE 0x77028
#define GCC_UFS_PHY_RX_SYMBOL0_ENABLE 0x7702c
#define GCC_UFS_PHY_UNIPRO_CORE_ENABLE 0x77068
#define GCC_UFS_PHY_ICE_CORE_ENABLE   0x77074
#define GCC_UFS_PHY_PHY_AUX_ENABLE    0x770b0
#define GCC_UFS_PHY_RX_SYMBOL1_ENABLE 0x770cc
#define GCC_AGGRE_UFS_PHY_AXI_ENABLE  0x770e4
#define GCC_UFS_BRANCH_ENABLE_BIT     0x1U

#endif
