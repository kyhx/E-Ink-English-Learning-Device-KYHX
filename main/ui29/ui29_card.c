/*
 * ui29_card.c — 2.9" 单词卡界面（横向 296x128），带二级菜单
 *
 * 物理屏是 128x296 竖屏：SSD1680 的 gate 固定在 296 行，所以横向显示
 * 只能把内容旋转 90° 写进同一块帧缓冲。所有绘制都走 put_px()：
 *
 *     phys_x = 127 - ly      (0..127，128 列)
 *     phys_y = lx            (0..295，296 行)
 *
 * 页面结构（二级）：
 *     主菜单 ─┬─ 单词      PREV/NEXT 翻词，REC 录音，PLAY 播放，SYNC 返回
 *             ├─ 复习日历  PREV/NEXT 换月，黑底白字=已复习，方框=今天
 *             ├─ 同步      Wi-Fi 状态（未启用）
 *             └─ 设置      无线网络 / 蓝牙 开关（PLAY 切换）
 *
 * 方向现场校正：`bench ui flip <0..3>`。
 * 布局常量与 tools/ui29_preview.py 一一对应，改一边必须改另一边。
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "sdkconfig.h"

#include "audio_in.h"
#include "audio_out.h"
#include "audio_vad.h"
#include "audio_wav.h"
#include "board/buttons.h"
#include "epd_ssd1680.h"

#include "ui29_card.h"
#include "ui29_font14.h"
#include "ui29_font24.h"
#include "ui29_words.h"

static const char *TAG = "ui29";

/* ---------------------------------------------------------------- 逻辑尺寸 */
#define LW          296
#define LH          128

/* ---------------------------------------------------------------- 布局常量 */
#define MARGIN      3
#define INNER       (LW - 2 * MARGIN)     /* 290 */
#define Y_SBAR      3
#define Y_DIV1      14

#define MENU_X      12
#define MENU_Y      18
#define MENU_ROW_H  26

#define COL_L       8
#define Y_WORD      18
#define WORD_SCALE  4
#define Y_ROW2      56                    /* 音标 + 释义同一行 */
#define Y_BAR       86
#define BAR_H       8
#define Y_HINT      106

#define CAL_CELL_W  41
#define CAL_HDR_Y   15
#define CAL_Y0      41
#define CAL_ROW_H   14

#define CJK         24
#define IPA_ADV     12

#define TMO_MS          8000
#define FULL_EVERY_N    20

/* ---------------------------------------------------------------- 页面状态 */
typedef enum {
    PAGE_MENU = 0,
    PAGE_CARD,
    PAGE_CAL,
    PAGE_SYNC,
    PAGE_SETTINGS,
    PAGE_COUNT,
} page_t;

typedef enum {
    CARD_READY = 0,
    CARD_RECORDING,
    CARD_SAVED,
    CARD_ERROR,
} card_state_t;

static page_t s_page;
static card_state_t s_card_state;
static int s_menu_sel;
static int s_word;
static int s_set_sel;
static int s_cal_year;
static int s_cal_month;
static int s_refresh_n;
static int s_flip;
static bool s_wifi_on;
static bool s_bt_on;
static uint32_t s_reviewed;          /* bit (day-1)：已复习的日期 */
static volatile bool s_redraw_req;
static char s_msg[48];

static uint8_t s_fb[EPD1680_FRAME_BYTES];

/* ---------------------------------------------------------------- 坐标映射 */

static void put_px(int x, int y, int color)
{
    if (x < 0 || x >= LW || y < 0 || y >= LH) return;

    int lx = x;
    int ly = y;
    if (s_flip & 2) lx = LW - 1 - lx;
    if (s_flip & 1) ly = LH - 1 - ly;

    epd1680_fb_pixel(s_fb, (EPD1680_WIDTH - 1) - ly, lx, (uint8_t)color);
}

static void hline(int x, int y, int w, int color)
{
    for (int i = 0; i < w; i++) put_px(x + i, y, color);
}

static void vline(int x, int y, int h, int color)
{
    for (int i = 0; i < h; i++) put_px(x, y + i, color);
}

static void box_rect(int x, int y, int w, int h, int color)
{
    hline(x, y, w, color);
    hline(x, y + h - 1, w, color);
    vline(x, y, h, color);
    vline(x + w - 1, y, h, color);
}

static void fill_rect(int x, int y, int w, int h, int color)
{
    for (int j = 0; j < h; j++) hline(x, y + j, w, color);
}

/* ---------------------------------------------------------------- 文本绘制 */

static int ascii_w(const char *s, int scale)
{
    int sc = scale > 0 ? scale : 1;
    return (int)strlen(s) * 5 * sc;
}

/* color: 1 = 黑字（默认），0 = 白字（用于反白） */
static void draw_ascii(int x, int y, const char *s, int scale, int color)
{
    int sc = scale > 0 ? scale : 1;
    int cx = x;
    for (const char *p = s; *p; p++) {
        const uint8_t *g = epd1680_ascii_glyph(*p);
        for (int col = 0; col < 5; col++) {
            uint8_t bits = g[col];
            for (int row = 0; row < 8; row++) {
                if (!(bits & (1u << row))) continue;
                int px = cx + col * sc;
                int py = y + row * sc;
                if (sc == 1) {
                    put_px(px, py, color);
                } else {
                    fill_rect(px, py, sc, sc, color);
                }
            }
        }
        cx += 5 * sc;
    }
}

static uint16_t utf8_next(const char *s, int *len)
{
    const unsigned char *p = (const unsigned char *)s;
    if (p[0] < 0x80) {
        *len = 1;
        return p[0];
    }
    if ((p[0] & 0xE0) == 0xC0 && (p[1] & 0xC0) == 0x80) {
        *len = 2;
        return (uint16_t)(((p[0] & 0x1F) << 6) | (p[1] & 0x3F));
    }
    if ((p[0] & 0xF0) == 0xE0 && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80) {
        *len = 3;
        return (uint16_t)(((p[0] & 0x0F) << 12) | ((p[1] & 0x3F) << 6) |
                          (p[2] & 0x3F));
    }
    *len = 1;
    return '?';
}

static int utf8_count(const char *s)
{
    int n = 0;
    while (*s) {
        int l = 0;
        (void)utf8_next(s, &l);
        s += l;
        n++;
    }
    return n;
}

static const ui29_glyph24_t *find24(uint16_t cp)
{
    for (int i = 0; i < UI29_FONT24_COUNT; i++) {
        if (k_font24[i].cp == cp) return &k_font24[i];
    }
    return NULL;
}

static const ui29_glyph14_t *find14(uint16_t cp)
{
    for (int i = 0; i < UI29_FONT14_COUNT; i++) {
        if (k_font14[i].cp == cp) return &k_font14[i];
    }
    return NULL;
}

/* 24x24 汉字，步进 24 px；color 0 = 白字 */
static int text24(int x, int y, const char *s, int color)
{
    int cx = x;
    const char *p = s;
    while (*p) {
        int n = 0;
        uint16_t cp = utf8_next(p, &n);
        const ui29_glyph24_t *g = find24(cp);
        if (g) {
            for (int j = 0; j < CJK; j++) {
                uint32_t bits = g->bmp[j];
                if (!bits) continue;
                for (int i = 0; i < CJK; i++) {
                    if (bits & (1u << (23 - i))) {
                        put_px(cx + i, y + j, color);
                    }
                }
            }
        }
        cx += CJK;
        p += n;
    }
    return cx - x;
}

/* 14x24 音标，步进 12 px */
static int text14(int x, int y, const char *s)
{
    int cx = x;
    const char *p = s;
    while (*p) {
        int n = 0;
        uint16_t cp = utf8_next(p, &n);
        const ui29_glyph14_t *g = find14(cp);
        if (g) {
            for (int j = 0; j < 24; j++) {
                uint16_t bits = g->bmp[j];
                if (!bits) continue;
                for (int i = 0; i < IPA_ADV + 2; i++) {
                    if (bits & (1u << (13 - i))) {
                        put_px(cx + i, y + j, 1);
                    }
                }
            }
        }
        cx += IPA_ADV;
        p += n;
    }
    return cx - x;
}

/* ---------------------------------------------------------------- 公共件 */

static void status_bar(const char *left, const char *right)
{
    draw_ascii(MARGIN, Y_SBAR, left, 1, 1);
    draw_ascii(LW - MARGIN - ascii_w(right, 1), Y_SBAR, right, 1, 1);
    hline(MARGIN, Y_DIV1, INNER, 1);
}

static void footer_bar(const char *text)
{
    box_rect(MARGIN, Y_HINT - 8, INNER, 24, 1);
    draw_ascii(MARGIN + 6, Y_HINT, text, 1, 1);
}

/* ---------------------------------------------------------------- 页面：菜单 */

static const char *const k_menu_items[] = {"单词", "复习日历", "同步", "设置"};
#define MENU_ITEMS ((int)(sizeof(k_menu_items) / sizeof(k_menu_items[0])))

static void draw_menu(void)
{
    status_bar("E-INK WORD", "MENU");
    for (int i = 0; i < MENU_ITEMS; i++) {
        int y = MENU_Y + i * MENU_ROW_H;
        if (i == s_menu_sel) {
            fill_rect(MARGIN, y - 2, INNER, MENU_ROW_H - 2, 1);
            text24(MENU_X, y, k_menu_items[i], 0);
            draw_ascii(LW - MARGIN - 12, y + 8, ">", 1, 0);
        } else {
            text24(MENU_X, y, k_menu_items[i], 1);
            draw_ascii(LW - MARGIN - 12, y + 8, ">", 1, 1);
        }
    }
}

/* ---------------------------------------------------------------- 页面：卡片 */

static void draw_card(void)
{
    const ui29_word_t *w = &k_words29[s_word];
    char idx[16];
    snprintf(idx, sizeof(idx), "%02d/%02d", s_word + 1, UI29_WORD_COUNT);

    status_bar("UNIT 01", idx);
    draw_ascii(COL_L, Y_WORD, w->word, WORD_SCALE, 1);

    /* 音标 + 中文释义同一行 */
    text14(COL_L, Y_ROW2, w->ipa);
    int mw = utf8_count(w->meaning) * CJK;
    int mx = COL_L + utf8_count(w->ipa) * IPA_ADV + 14;
    if (mx + mw > LW - MARGIN) mx = LW - MARGIN - mw;
    if (mx < COL_L) mx = COL_L;
    text24(mx, Y_ROW2, w->meaning, 1);

    /* 进度条 */
    box_rect(MARGIN, Y_BAR, INNER, BAR_H, 1);
    int fill = (INNER - 4) * (s_word + 1) / UI29_WORD_COUNT;
    if (fill > 0) fill_rect(MARGIN + 2, Y_BAR + 2, fill, BAR_H - 4, 1);

    /* 状态：右下方小字，避免和音标/释义抢位置 */
    if (s_card_state != CARD_READY) {
        draw_ascii(MARGIN + 4, Y_BAR - 12, s_msg, 1, 1);
    }

    footer_bar("PREV NEXT PLAY   REC SYNC HOLD");
}

/* ---------------------------------------------------------------- 页面：日历 */

static bool is_leap(int y)
{
    return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
}

static int days_in_month(int y, int m)
{
    static const int d[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (m == 2 && is_leap(y)) return 29;
    return d[m - 1];
}

/* 0=周日 .. 6=周六 */
static int weekday_of(int y, int m, int d)
{
    static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    if (m < 3) y -= 1;
    return (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;
}

static void draw_calendar(void)
{
    char head[24];
    snprintf(head, sizeof(head), "%04d-%02d  SYNC=BACK", s_cal_year, s_cal_month);
    status_bar("CALENDAR", head);

    static const char *const wd = "日一二三四五六";
    for (int i = 0; i < 7; i++) {
        char ch[2] = {0, 0};
        /* wd 是 UTF-8 字符串，每个汉字 3 字节 */
        memcpy(ch, wd + i * 3, 3);
        text24(MARGIN + i * CAL_CELL_W + (CAL_CELL_W - CJK) / 2, CAL_HDR_Y, ch, 1);
    }

    int first = weekday_of(s_cal_year, s_cal_month, 1);
    int ndays = days_in_month(s_cal_year, s_cal_month);
    bool is_current = false;
    time_t now = time(NULL);
    struct tm tmv;
    localtime_r(&now, &tmv);
    if (tmv.tm_year + 1900 == s_cal_year && tmv.tm_mon + 1 == s_cal_month) {
        is_current = true;
    }

    for (int d = 1; d <= ndays; d++) {
        int slot = first + d - 1;
        int r = slot / 7;
        int c = slot % 7;
        if (r >= 6) break;

        int cx = MARGIN + c * CAL_CELL_W;
        int cy = CAL_Y0 + r * CAL_ROW_H;
        char txt[16];
        snprintf(txt, sizeof(txt), "%d", d);
        int tw = ascii_w(txt, 1);
        int x = cx + (CAL_CELL_W - tw) / 2;

        bool done = (s_reviewed >> (d - 1)) & 1;
        if (done) {
            fill_rect(cx + 8, cy, CAL_CELL_W - 16, 11, 1);
            draw_ascii(x, cy + 2, txt, 1, 0);
        } else {
            draw_ascii(x, cy + 2, txt, 1, 1);
        }
        if (is_current && d == tmv.tm_mday) {
            box_rect(cx + 6, cy - 1, CAL_CELL_W - 12, 13, 1);
        }
    }
}

/* ---------------------------------------------------------------- 页面：同步 */

static void draw_sync(void)
{
    status_bar("SYNC", s_wifi_on ? "WIFI ON" : "WIFI OFF");
    text24(MARGIN, 18, "无线同步", 1);
    draw_ascii(MARGIN, 50, "SSID", 1, 1);
    draw_ascii(MARGIN + 44, 50, "EPD-Word-01", 1, 1);
    draw_ascii(MARGIN, 64, "URL", 1, 1);
    draw_ascii(MARGIN + 44, 64, "192.168.4.1", 1, 1);
    text24(MARGIN, 80, s_wifi_on ? "已开启" : "未启用", 1);
    draw_ascii(MARGIN, Y_HINT + 4, "SYNC = BACK", 1, 1);
}

/* ---------------------------------------------------------------- 页面：设置 */

static void draw_settings(void)
{
    status_bar("SETTINGS", "MENU");
    const char *labels[2] = {"无线网络", "蓝牙"};
    const char *states[2] = {s_wifi_on ? "开启" : "关闭", s_bt_on ? "开启" : "关闭"};

    for (int i = 0; i < 2; i++) {
        int y = 18 + i * 38;
        if (i == s_set_sel) {
            box_rect(MARGIN, y - 2, INNER, 30, 1);
        }
        text24(MARGIN + 10, y, labels[i], 1);
        int sw = utf8_count(states[i]) * CJK;
        text24(LW - MARGIN - 10 - sw, y, states[i], 1);
    }
    draw_ascii(MARGIN, Y_HINT + 4, "PLAY = TOGGLE   SYNC = BACK", 1, 1);
}

/* ---------------------------------------------------------------- 刷新 */

static void render(void)
{
    epd1680_fb_clear(s_fb, 1);
    switch (s_page) {
    case PAGE_CARD:
        draw_card();
        break;
    case PAGE_CAL:
        draw_calendar();
        break;
    case PAGE_SYNC:
        draw_sync();
        break;
    case PAGE_SETTINGS:
        draw_settings();
        break;
    case PAGE_MENU:
    default:
        draw_menu();
        break;
    }
}

static void refresh(bool force_full)
{
    int64_t t0 = esp_timer_get_time();
    bool ok;
    bool full = force_full || s_refresh_n == 0 ||
                (s_refresh_n % FULL_EVERY_N) == 0;

    if (full) {
        ok = epd1680_full(s_fb, TMO_MS);
    } else {
        ok = epd1680_fast(s_fb, TMO_MS);
    }
    ESP_LOGI(TAG, "%s refresh %s %lld ms", full ? "full" : "partial",
             ok ? "ok" : "FAIL", (long long)((esp_timer_get_time() - t0) / 1000));
    s_refresh_n++;
}

static void go(page_t p)
{
    s_page = p;
    render();
    refresh(false);
}

/* ---------------------------------------------------------------- 录音 */

static void mark_reviewed_today(void)
{
    time_t now = time(NULL);
    struct tm tmv;
    localtime_r(&now, &tmv);
    s_reviewed |= (1u << tmv.tm_mday);
}

static void finish_recording(void)
{
    int16_t *pcm = NULL;
    size_t frames = 0;
    audio_capture_stats_t stats;
    esp_err_t err = audio_in_stop(&pcm, &frames, &stats);

    s_card_state = CARD_READY;
    snprintf(s_msg, sizeof(s_msg), "READY");

    if (err != ESP_OK || frames == 0) {
        ESP_LOGE(TAG, "record stop failed: %s", esp_err_to_name(err));
        s_card_state = CARD_ERROR;
        snprintf(s_msg, sizeof(s_msg), "SAVE ERR");
        render();
        refresh(false);
        return;
    }

    audio_vad_result_t vad;
    err = audio_vad_analyze(pcm, frames, &vad);
    if (err != ESP_OK || !vad.has_speech) {
        ESP_LOGW(TAG, "no speech snr=%.1f", vad.snr_db);
        s_card_state = CARD_ERROR;
        snprintf(s_msg, sizeof(s_msg), "NO SPEECH");
        audio_in_free(pcm);
        render();
        refresh(false);
        return;
    }

    size_t n = vad.speech_end - vad.speech_start;
    char path[160];
    err = audio_wav_make_rec_path(path, sizeof(path));
    if (err == ESP_OK) {
        err = audio_wav_write(path, pcm + vad.speech_start, n);
    }
    if (err == ESP_OK) {
        char dur[16];
        audio_wav_duration_str((uint32_t)n, dur, sizeof(dur));
        ESP_LOGI(TAG, "saved %s (%u frames, snr=%.1f dB)", path,
                 (unsigned)n, vad.snr_db);
        s_card_state = CARD_SAVED;
        snprintf(s_msg, sizeof(s_msg), "SAVED %s", dur);
        mark_reviewed_today();
    } else {
        ESP_LOGE(TAG, "save failed: %s", esp_err_to_name(err));
        s_card_state = CARD_ERROR;
        snprintf(s_msg, sizeof(s_msg), "SAVE ERR");
    }
    audio_in_free(pcm);

    render();
    refresh(false);
    vTaskDelay(pdMS_TO_TICKS(1200));
    s_card_state = CARD_READY;
    snprintf(s_msg, sizeof(s_msg), "READY");
    render();
    refresh(false);
}

static void start_recording(void)
{
    esp_err_t err = audio_in_start(CONFIG_APP_AUDIO_RECORD_MS);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "audio_in_start: %s", esp_err_to_name(err));
        s_card_state = CARD_ERROR;
        snprintf(s_msg, sizeof(s_msg), "MIC ERR");
    } else {
        s_card_state = CARD_RECORDING;
        snprintf(s_msg, sizeof(s_msg), "REC 2.0s");
    }
    render();
    refresh(false);
}

static void play_last(void)
{
    char path[160];
    if (audio_wav_latest(path, sizeof(path)) != ESP_OK) {
        s_card_state = CARD_ERROR;
        snprintf(s_msg, sizeof(s_msg), "NO REC");
    } else {
        snprintf(s_msg, sizeof(s_msg), "PLAY");
        (void)audio_out_play_wav(path);
        s_card_state = CARD_READY;
    }
    render();
    refresh(false);
}

/* ---------------------------------------------------------------- 对外调试 */

void ui29_goto(int page)
{
    if (page < 0 || page >= PAGE_COUNT) return;
    s_page = (page_t)page;
    s_redraw_req = true;
    ESP_LOGI(TAG, "goto page %d", page);
}

void ui29_set_flip(int flip)
{
    s_flip = flip & 0x3;
    s_redraw_req = true;
    ESP_LOGI(TAG, "flip=%d", s_flip);
}

/* ---------------------------------------------------------------- 主循环 */

void ui29_run(void)
{
    time_t now = time(NULL);
    struct tm tmv;
    localtime_r(&now, &tmv);
    s_cal_year = tmv.tm_year + 1900;
    s_cal_month = tmv.tm_mon + 1;

    ESP_LOGI(TAG, "menu start, %d words, %04d-%02d", UI29_WORD_COUNT,
             s_cal_year, s_cal_month);

    s_page = PAGE_MENU;
    s_menu_sel = 0;
    s_word = 0;
    s_card_state = CARD_READY;
    snprintf(s_msg, sizeof(s_msg), "READY");

    render();
    refresh(true);

    uint32_t last_poll = 0;
    bool recording = false;

    for (;;) {
        uint32_t ms = (uint32_t)(esp_timer_get_time() / 1000);
        if (ms - last_poll < 10) {
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }
        last_poll = ms;

        if (s_redraw_req) {
            s_redraw_req = false;
            render();
            refresh(true);
            continue;
        }

        if (recording && audio_in_is_done()) {
            recording = false;
            finish_recording();
            continue;
        }

        button_event_t ev = buttons_poll(ms);
        if (ev == BUTTON_EVENT_NONE) continue;
        ESP_LOGI(TAG, "event %s", buttons_event_name(ev));

        if (recording) continue;

        switch (s_page) {
        case PAGE_MENU:
            if (ev == BUTTON_EVENT_PREV_SHORT) {
                s_menu_sel = (s_menu_sel + MENU_ITEMS - 1) % MENU_ITEMS;
                render();
                refresh(false);
            } else if (ev == BUTTON_EVENT_NEXT_SHORT) {
                s_menu_sel = (s_menu_sel + 1) % MENU_ITEMS;
                render();
                refresh(false);
            } else if (ev == BUTTON_EVENT_PLAY_SHORT) {
                if (s_menu_sel == 0) go(PAGE_CARD);
                else if (s_menu_sel == 1) go(PAGE_CAL);
                else if (s_menu_sel == 2) go(PAGE_SYNC);
                else go(PAGE_SETTINGS);
            } else if (ev == BUTTON_EVENT_SYNC_SHORT) {
                go(PAGE_SYNC);
            }
            break;

        case PAGE_CARD:
            if (ev == BUTTON_EVENT_PREV_SHORT) {
                s_word = (s_word + UI29_WORD_COUNT - 1) % UI29_WORD_COUNT;
                render();
                refresh(false);
            } else if (ev == BUTTON_EVENT_NEXT_SHORT) {
                s_word = (s_word + 1) % UI29_WORD_COUNT;
                render();
                refresh(false);
            } else if (ev == BUTTON_EVENT_RECORD_SHORT) {
                start_recording();
                recording = true;
            } else if (ev == BUTTON_EVENT_PLAY_SHORT) {
                play_last();
            } else if (ev == BUTTON_EVENT_SYNC_SHORT ||
                       ev == BUTTON_EVENT_SYNC_LONG) {
                go(PAGE_MENU);
            }
            break;

        case PAGE_CAL:
            if (ev == BUTTON_EVENT_PREV_SHORT) {
                if (--s_cal_month < 1) {
                    s_cal_month = 12;
                    s_cal_year--;
                }
                render();
                refresh(false);
            } else if (ev == BUTTON_EVENT_NEXT_SHORT) {
                if (++s_cal_month > 12) {
                    s_cal_month = 1;
                    s_cal_year++;
                }
                render();
                refresh(false);
            } else if (ev == BUTTON_EVENT_SYNC_SHORT ||
                       ev == BUTTON_EVENT_SYNC_LONG) {
                go(PAGE_MENU);
            }
            break;

        case PAGE_SYNC:
            if (ev == BUTTON_EVENT_SYNC_SHORT || ev == BUTTON_EVENT_SYNC_LONG ||
                ev == BUTTON_EVENT_PLAY_SHORT) {
                go(PAGE_MENU);
            }
            break;

        case PAGE_SETTINGS:
            if (ev == BUTTON_EVENT_PREV_SHORT || ev == BUTTON_EVENT_NEXT_SHORT) {
                s_set_sel ^= 1;
                render();
                refresh(false);
            } else if (ev == BUTTON_EVENT_PLAY_SHORT) {
                if (s_set_sel == 0) {
                    s_wifi_on = !s_wifi_on;
                } else {
                    s_bt_on = !s_bt_on;
                }
                render();
                refresh(false);
            } else if (ev == BUTTON_EVENT_SYNC_SHORT ||
                       ev == BUTTON_EVENT_SYNC_LONG) {
                go(PAGE_MENU);
            }
            break;

        default:
            go(PAGE_MENU);
            break;
        }
    }
}
