/*
 * word_view.c
 *
 * 单词卡片布局实现。所有函数只操作内存帧缓冲，不触碰 SPI / 面板，
 * 因此可以在 PC 上离线复刻（见 tools/preview_demo.py）。
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <string.h>

#include "epd_ssd1619a.h"
#include "word_view.h"

/* 单词区：最终用 400x300 全帧 transition-only 刷新 */
#define AREA_X   40
#define AREA_Y   90
#define AREA_W   320
#define AREA_H   120

/* 演示词表（50 个） */
static const char *const s_words[] = {
    "apple", "book", "cat", "dog", "eat",
    "fish", "good", "hand", "idea", "jump",
    "kind", "love", "moon", "name", "open",
    "park", "quiz", "rain", "sun", "tree",
    "unit", "very", "word", "yard", "zero",
    "bird", "blue", "cold", "door", "easy",
    "face", "game", "home", "iron", "just",
    "keep", "light", "move", "nice", "only",
    "play", "quick", "read", "ship", "time",
    "upon", "view", "warm", "year", "zoom"
};

int word_view_count(void)
{
    return (int)(sizeof(s_words) / sizeof(s_words[0]));
}

const char *word_view_word(int idx)
{
    if (idx < 0 || idx >= word_view_count()) {
        return "";
    }
    return s_words[idx];
}

void word_view_base(uint8_t *fb)
{
    epd_fb_clear(fb, 1);
    epd_fb_rect(fb, 0, 0, EPD_WIDTH, EPD_HEIGHT, 0);

    const char *title = "50 WORDS";
    int w = epd_fb_text_width_scaled(title, 3);
    epd_fb_text_scaled(fb, (EPD_WIDTH - w) / 2, 35, title, 0, 3);

    const char *foot = "partial refresh / 1s per word";
    w = epd_fb_text_width(foot);
    epd_fb_text(fb, (EPD_WIDTH - w) / 2, 265, foot, 0);
}

void word_view_card(uint8_t *dst, const uint8_t *base, int idx)
{
    memcpy(dst, base, EPD_FRAME_BYTES);

    char num[16];
    snprintf(num, sizeof(num), "%02d/%d", idx + 1, word_view_count());
    int w = epd_fb_text_width_scaled(num, 2);
    epd_fb_text_scaled(dst, AREA_X + (AREA_W - w) / 2, AREA_Y + 8, num, 0, 2);

    const char *word = word_view_word(idx);
    w = epd_fb_text_width_scaled(word, 4);
    epd_fb_text_scaled(dst, AREA_X + (AREA_W - w) / 2, AREA_Y + 45, word, 0, 4);
}

void word_view_banner(uint8_t *fb, const char *text)
{
    int w = epd_fb_text_width_scaled(text, 4);
    epd_fb_text_scaled(fb, (EPD_WIDTH - w) / 2, 140, text, 0, 4);
}
