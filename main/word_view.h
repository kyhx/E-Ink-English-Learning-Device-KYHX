/*
 * word_view.h
 *
 * 单词卡片的屏显布局（纯内存帧缓冲绘制，不含任何 SPI 操作）
 *
 * 布局约定（400x300，1bpp，1=白 0=黑）：
 *   y  35   标题 "50 WORDS"（3 倍字）
 *   y  90   单词区起始（AREA_X/AREA_Y/AREA_W/AREA_H）
 *   y 265   页脚说明
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef WORD_VIEW_H
#define WORD_VIEW_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 词表大小 */
int word_view_count(void);

/* 第 idx 个单词（0 起），越界返回 "" */
const char *word_view_word(int idx);

/* 在 fb 上绘制固定底图：外框 + 标题 + 页脚。fb 需为 EPD_FRAME_BYTES 字节 */
void word_view_base(uint8_t *fb);

/* 把底图 base 复制到 dst，再叠加第 idx 个单词（序号 2 倍字 + 单词 4 倍字） */
void word_view_card(uint8_t *dst, const uint8_t *base, int idx);

/* 在底图上居中绘制一段 4 倍字的大字（用于 DONE 之类的提示） */
void word_view_banner(uint8_t *fb, const char *text);

#ifdef __cplusplus
}
#endif
#endif /* WORD_VIEW_H */
