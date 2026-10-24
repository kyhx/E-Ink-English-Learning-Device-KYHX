/*
 * epd_lut_partial.h
 *
 * 70-byte SSD1619A partial-refresh LUT extracted from the OSHWHub project
 *   "某价签改装极简墨水屏开发板 手改灰度" / HalfSweetStudio EPaperDrive
 *   https://oshwhub.com/shilihualang/han-jia-qian-gai-zhuang-ji-jian-kai-fa-ban
 *   https://github.com/HalfSweetStudio/EPaperDrive
 *
 * EPaperDrive is AGPL-3.0. This data/sequence is used only for local
 * experiments; do not redistribute it in an MIT release without compliance.
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#ifndef EPD_LUT_PARTIAL_H
#define EPD_LUT_PARTIAL_H

#include <stdint.h>

/* LUTDefault_part_opm42: 70 bytes, includes the 0x32 command byte in the
 * original; here only the 70 data bytes are stored. */
static const uint8_t epd_lut_partial_70[70] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* L0 BB */
    0x82, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* L1 BW */
    0x50, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* L2 WB */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* L3 WW */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* L4 VCOM */

    0x08, 0x08, 0x00, 0x08, 0x01,             /* TP0 */
    0x00, 0x00, 0x00, 0x00, 0x01,             /* TP1 */
    0x00, 0x00, 0x00, 0x00, 0x00,             /* TP2 */
    0x00, 0x00, 0x00, 0x00, 0x00,             /* TP3 */
    0x00, 0x00, 0x00, 0x00, 0x00,             /* TP4 */
    0x00, 0x00, 0x00, 0x00, 0x00,             /* TP5 */
    0x00, 0x00, 0x00, 0x00, 0x00,             /* TP6 */
};

#endif /* EPD_LUT_PARTIAL_H */
