/*
 * ui29_card.c — 2.9" 单词卡界面（横向 296x128）
 *
 * 物理屏是 128x296 竖屏：SSD1680 的 gate 固定在 296 行，所以横向显示
 * 只能把内容旋转 90° 写进同一块帧缓冲。这里所有绘制都走 put_px()，
 * 由它做「横向逻辑坐标 -> 物理坐标」的映射：
 *
 *     phys_x = 127 - ly      (0..127，正好是 128 列)
 *     phys_y = lx            (0..295，正好是 296 行)
 *
 * 如果实机上方向倒了/镜像了，用 `bench ui flip <0..3>` 现场翻，不用重烧：
 *     bit0 = 上下翻（翻转 ly），bit1 = 左右翻（翻转 lx）
 *
 * 布局常量与 tools/ui29_preview.py 一一对应：改一边必须改另一边。
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <string.h>

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
#include "ui29_font12.h"
#include "ui29_font16.h"
#include "ui29_words.h"

static const char *TAG = "ui29";

/* ---------------------------------------------------------------- 逻辑尺寸 */
#define LW          296          /* 横向宽（对应物理 296 行） */
#define LH          128          /* 横向高（对应物理 128 列） */

/* ---------------------------------------------------------------- 布局常量 */
#define MARGIN      4
#define INNER       (LW - 2 * MARGIN)     /* 288 */
#define Y_SBAR      5
#define Y_DIV1      16
#define COL_L       8
#define COL_R       176
#define Y_WORD      21
#define WORD_SCALE  3
#define Y_IPA       55
#define Y_MEAN      27
#define MEAN_LINE_H 20
#define Y_BIG       55
#define Y_SMALL     81
#define Y_BAR       103
#define BAR_H       6
#define Y_HINT      115

#define TMO_MS          8000
#define FULL_EVERY_N    20

/* ---------------------------------------------------------------- 页面状态 */
typedef enum {
    PAGE_CARD = 0,
    PAGE_HOME,
    PAGE_SYNC,
} page_t;

typedef enum {
    CARD_READY = 0,
    CARD_RECORDING,
    CARD_SAVED,
    CARD_PLAYING,
    CARD_ERROR,
} card_state_t;

static page_t s_page;
static card_state_t s_state;
static int s_idx;
static int s_refresh_n;
static int s_flip;                    /* bit0: 上下翻, bit1: 左右翻 */
static volatile bool s_redraw_req;
static char s_msg[48];
static char s_big[24];

static uint8_t s_fb[EPD1680_FRAME_BYTES];

/* ---------------------------------------------------------------- 坐标映射 */

static void put_px(int x, int y, int color)
{
    if (x < 0 || x >= LW || y < 0 || y >= LH) return;

    int lx = x;
    int ly = y;
    if (s_flip & 2) lx = LW - 1 - lx;
    if (s_flip & 1) ly = LH - 1 - ly;

    int px = (EPD1680_WIDTH - 1) - ly;
    int py = lx;
    epd1680_fb_pixel(s_fb, px, py, (uint8_t)color);
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

static void draw_ascii(int x, int y, const char *s, int scale)
{
    int sc = scale > 0 ? scale : 1;
    int cx = x;
    for (const char *p = s; *p; p++) {
        const uint8_t *g = epd1680_ascii_glyph(*p);
        for (int col = 0; col < 5; col++) {
            uint8_t bits = g[col];
            for (int row = 0; row < 8; row++) {
                if (!(bits & (1u << row))) continue;
                if (sc == 1) {
                    put_px(cx + col, y + row, 0);
                } else {
                    fill_rect(cx + col * sc, y + row * sc, sc, sc, 0);
                }
            }
        }
        cx += 5 * sc;
    }
}

/* UTF-8 解码：返回码位，*len 为消耗字节数 */
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

static const ui29_glyph16_t *find16(uint16_t cp)
{
    for (int i = 0; i < UI29_FONT16_COUNT; i++) {
        if (k_font16[i].cp == cp) return &k_font16[i];
    }
    return NULL;
}

static const ui29_glyph12_t *find12(uint16_t cp)
{
    for (int i = 0; i < UI29_FONT12_COUNT; i++) {
        if (k_font12[i].cp == cp) return &k_font12[i];
    }
    return NULL;
}

/* 16x16 汉字，步进 16 px */
static int text16(int x, int y, const char *s)
{
    int cx = x;
    const char *p = s;
    while (*p) {
        int n = 0;
        uint16_t cp = utf8_next(p, &n);
        const ui29_glyph16_t *g = find16(cp);
        if (g) {
            for (int j = 0; j < 16; j++) {
                uint16_t bits = (uint16_t)((g->bmp[j * 2] << 8) |
                                           g->bmp[j * 2 + 1]);
                for (int i = 0; i < 16; i++) {
                    if (bits & (1u << (15 - i))) {
                        put_px(cx + i, y + j, 0);
                    }
                }
            }
        }
        cx += 16;
        p += n;
    }
    return cx - x;
}

/* 12x20 音标，步进 10 px */
static int text12(int x, int y, const char *s)
{
    int cx = x;
    const char *p = s;
    while (*p) {
        int n = 0;
        uint16_t cp = utf8_next(p, &n);
        const ui29_glyph12_t *g = find12(cp);
        if (g) {
            for (int j = 0; j < 20; j++) {
                uint16_t bits = g->bmp[j];
                for (int i = 0; i < 12; i++) {
                    if (bits & (1u << (11 - i))) {
                        put_px(cx + i, y + j, 0);
                    }
                }
            }
        }
        cx += 10;
        p += n;
    }
    return cx - x;
}

/* ---------------------------------------------------------------- 绘制页面 */

static void draw_status_bar(const char *left, const char *right)
{
    draw_ascii(MARGIN, Y_SBAR, left, 1);
    draw_ascii(LW - MARGIN - ascii_w(right, 1), Y_SBAR, right, 1);
    hline(MARGIN, Y_DIV1, INNER, 0);
}

static void draw_progress(int idx, int total)
{
    box_rect(MARGIN, Y_BAR, INNER, BAR_H, 0);
    int fill_w = (INNER - 4) * (idx + 1) / total;
    if (fill_w > 0) {
        fill_rect(MARGIN + 2, Y_BAR + 2, fill_w, BAR_H - 4, 0);
    }
}

static void draw_hint(void)
{
    draw_ascii(MARGIN, Y_HINT, "PREV NEXT PLAY    REC SYNC HOLD", 1);
}

/* 每行 7 个汉字；横向右栏只有 116 px，多出来的行不再画 */
static void draw_meaning_1line(const char *meaning)
{
    char line[64];
    const char *p = meaning;
    int cnt = 0;
    size_t used = 0;
    line[0] = '\0';
    while (*p && cnt < 7) {
        int n = 0;
        (void)utf8_next(p, &n);
        if (used + (size_t)n + 1 >= sizeof(line)) break;
        memcpy(line + used, p, (size_t)n);
        used += (size_t)n;
        line[used] = '\0';
        p += n;
        cnt++;
    }
    text16(COL_R, Y_MEAN, line);
}

static void draw_card_page(void)
{
    const ui29_word_t *w = &k_words29[s_idx];
    char idxbuf[16];
    snprintf(idxbuf, sizeof(idxbuf), "%02d/%02d", s_idx + 1, UI29_WORD_COUNT);

    draw_status_bar("UNIT 01", idxbuf);

    draw_ascii(COL_L, Y_WORD, w->word, WORD_SCALE);
    text12(COL_L, Y_IPA, w->ipa);

    draw_meaning_1line(w->meaning);
    text16(COL_R, Y_BIG, s_big);
    if (s_msg[0]) {
        text16(COL_R, Y_SMALL, s_msg);
    }

    draw_progress(s_idx, UI29_WORD_COUNT);
    draw_hint();
}

static void draw_home_page(void)
{
    draw_status_bar("E-INK WORD", "HOME");
    text16(MARGIN + 4, 28, "单词学习");
    draw_ascii(MARGIN + 4, 56, "DAILY", 2);

    box_rect(150, 24, 138, 72, 0);
    const char *labels[3] = {"TOTAL", "TODAY", "STREAK"};
    char values[3][8];
    snprintf(values[0], sizeof(values[0]), "%d", UI29_WORD_COUNT);
    snprintf(values[1], sizeof(values[1]), "0");
    snprintf(values[2], sizeof(values[2]), "0");

    int y = 32;
    for (int i = 0; i < 3; i++) {
        draw_ascii(160, y, labels[i], 1);
        draw_ascii(278 - ascii_w(values[i], 1), y, values[i], 1);
        for (int x = 158; x < 280; x += 5) {
            hline(x, y + 12, 3, 0);
        }
        y += 20;
    }
    draw_hint();
}

static void draw_sync_page(void)
{
    draw_status_bar("SYNC", "WIFI");
    text16(MARGIN + 4, 26, "无线同步");
    text16(220, 28, "未启用");

    box_rect(MARGIN + 4, 52, 200, 44, 0);
    draw_ascii(MARGIN + 12, 58, "SSID", 1);
    draw_ascii(MARGIN + 12, 70, "EPD-Word-01", 1);
    draw_ascii(MARGIN + 110, 58, "URL", 1);
    draw_ascii(MARGIN + 110, 70, "192.168.4.1", 1);

    draw_ascii(MARGIN, Y_HINT, "SYNC = STOP AP", 1);
}

/* ---------------------------------------------------------------- 刷新 */
static void render(void)
{
    epd1680_fb_clear(s_fb, 1);
    switch (s_page) {
    case PAGE_HOME:
        draw_home_page();
        break;
    case PAGE_SYNC:
        draw_sync_page();
        break;
    case PAGE_CARD:
    default:
        draw_card_page();
        break;
    }
}

static void refresh(bool force_full)
{
    int64_t t0 = esp_timer_get_time();
    bool ok;

    if (force_full || s_refresh_n == 0 || (s_refresh_n % FULL_EVERY_N) == 0) {
        ok = epd1680_full(s_fb, TMO_MS);
        ESP_LOGI(TAG, "full refresh %s %lld ms", ok ? "ok" : "FAIL",
                 (long long)((esp_timer_get_time() - t0) / 1000));
    } else {
        ok = epd1680_fast(s_fb, TMO_MS);
        ESP_LOGI(TAG, "partial refresh %s %lld ms", ok ? "ok" : "FAIL",
                 (long long)((esp_timer_get_time() - t0) / 1000));
    }
    s_refresh_n++;
}

static void set_status(card_state_t st, const char *big, const char *msg)
{
    s_state = st;
    snprintf(s_big, sizeof(s_big), "%s", big ? big : "");
    snprintf(s_msg, sizeof(s_msg), "%s", msg ? msg : "");
}

/* ---------------------------------------------------------------- 录音 */

static void finish_recording(void)
{
    int16_t *pcm = NULL;
    size_t frames = 0;
    audio_capture_stats_t stats;
    esp_err_t err = audio_in_stop(&pcm, &frames, &stats);
    if (err != ESP_OK || frames == 0) {
        ESP_LOGE(TAG, "record stop failed: %s", esp_err_to_name(err));
        set_status(CARD_ERROR, "错误", "请重试");
        render();
        refresh(false);
        return;
    }

    audio_vad_result_t vad;
    err = audio_vad_analyze(pcm, frames, &vad);
    if (err != ESP_OK || !vad.has_speech) {
        ESP_LOGW(TAG, "no speech snr=%.1f", vad.snr_db);
        set_status(CARD_ERROR, "错误", "无语音");
        render();
        refresh(false);
        audio_in_free(pcm);
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
        set_status(CARD_SAVED, "已保存", "评分待接入");
    } else {
        ESP_LOGE(TAG, "save failed: %s", esp_err_to_name(err));
        set_status(CARD_ERROR, "错误", "保存失败");
    }
    audio_in_free(pcm);

    render();
    refresh(false);
    vTaskDelay(pdMS_TO_TICKS(1200));
    set_status(CARD_READY, "就绪", "按录音键跟读");
    render();
    refresh(false);
}

static void start_recording(void)
{
    esp_err_t err = audio_in_start(CONFIG_APP_AUDIO_RECORD_MS);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "audio_in_start: %s", esp_err_to_name(err));
        set_status(CARD_ERROR, "错误", "麦克风不可用");
    } else {
        set_status(CARD_RECORDING, "录音中", "正在录音");
    }
    render();
    refresh(false);
}

static void play_last(void)
{
    char path[160];
    if (audio_wav_latest(path, sizeof(path)) != ESP_OK) {
        set_status(CARD_ERROR, "错误", "没有录音");
    } else {
        set_status(CARD_PLAYING, "播放中", "听发音");
        (void)audio_out_play_wav(path);
        set_status(CARD_READY, "就绪", "按录音键跟读");
    }
    render();
    refresh(false);
}

/* ---------------------------------------------------------------- 对外调试 */

void ui29_set_flip(int flip)
{
    s_flip = flip & 0x3;
    s_redraw_req = true;
    ESP_LOGI(TAG, "flip=%d", s_flip);
}

/* ---------------------------------------------------------------- 主循环 */

void ui29_run(void)
{
    ESP_LOGI(TAG, "word card start: %d words, landscape %dx%d",
             UI29_WORD_COUNT, LW, LH);

    s_page = PAGE_CARD;
    s_idx = 0;
    s_refresh_n = 0;
    set_status(CARD_READY, "就绪", "按录音键跟读");

    render();
    refresh(true);            /* 首次全刷，建立局刷基准 */

    uint32_t last_poll = 0;
    bool recording = false;

    for (;;) {
        uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
        if (now - last_poll < 10) {
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }
        last_poll = now;

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

        button_event_t ev = buttons_poll(now);
        if (ev == BUTTON_EVENT_NONE) {
            continue;
        }
        ESP_LOGI(TAG, "event %s", buttons_event_name(ev));

        if (recording) {
            continue;         /* 录音期间忽略按键 */
        }

        switch (ev) {
        case BUTTON_EVENT_NEXT_SHORT:
            if (s_page == PAGE_CARD) {
                s_idx = (s_idx + 1) % UI29_WORD_COUNT;
                set_status(CARD_READY, "就绪", "按录音键跟读");
                render();
                refresh(false);
            }
            break;
        case BUTTON_EVENT_PREV_SHORT:
            if (s_page == PAGE_CARD) {
                s_idx = (s_idx + UI29_WORD_COUNT - 1) % UI29_WORD_COUNT;
                set_status(CARD_READY, "就绪", "按录音键跟读");
                render();
                refresh(false);
            }
            break;
        case BUTTON_EVENT_NEXT_LONG:
            s_page = (s_page == PAGE_HOME) ? PAGE_CARD : PAGE_HOME;
            render();
            refresh(false);
            break;
        case BUTTON_EVENT_RECORD_SHORT:
            if (s_page == PAGE_CARD) {
                start_recording();
                recording = true;
            }
            break;
        case BUTTON_EVENT_PLAY_SHORT:
            if (s_page == PAGE_HOME) {
                s_page = PAGE_CARD;
                render();
                refresh(false);
            } else {
                play_last();
            }
            break;
        case BUTTON_EVENT_SYNC_SHORT:
            s_page = (s_page == PAGE_SYNC) ? PAGE_CARD : PAGE_SYNC;
            render();
            refresh(false);
            break;
        default:
            break;
        }
    }
}
