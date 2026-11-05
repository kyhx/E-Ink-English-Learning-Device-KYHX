/*
 * ui29_card.c — 2.9" (128x296) 单词卡界面
 *
 * 布局常量与 tools/ui29_preview.py 一一对应：改一边必须改另一边，
 * 否则 PC 预览和实机就对不上了。
 *
 * 刷新策略：
 *   - 首次上电全刷（2023 ms）建立局刷基准并清残影
 *   - 之后每次换页用整屏局刷（468 ms）
 *   - 每 20 次局刷插一次全刷，压住残影累积
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

/* ---------------------------------------------------------------- 布局常量 */
#define MARGIN      4
#define INNER       (EPD1680_WIDTH - 2 * MARGIN)   /* 120 */
#define Y_SBAR      2
#define Y_DIV1      12
#define Y_WORD      24
#define WORD_SCALE  4
#define Y_IPA       60
#define Y_DIV2      84
#define Y_MEAN      92
#define MEAN_LINE_H 20
#define Y_BOX       148
#define BOX_H       92
#define Y_BAR       254
#define BAR_H       6
#define Y_HINT1     268
#define Y_HINT2     278

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
static char s_msg[32];        /* 状态框副行（UTF-8） */
static char s_big[16];        /* 状态框主行（UTF-8） */

static uint8_t s_fb[EPD1680_FRAME_BYTES];

/* ---------------------------------------------------------------- 文本绘制 */

/* 5x7 ASCII（驱动自带）；scale=1 用原函数，scale>1 用放大版 */
static void draw_ascii(int x, int y, const char *s, int scale)
{
    if (scale <= 1) {
        epd1680_fb_text(s_fb, x, y, s, 0);
    } else {
        epd1680_fb_text_scaled(s_fb, x, y, s, 0, scale);
    }
}

static int ascii_w(const char *s, int scale)
{
    return (int)strlen(s) * 5 * (scale > 0 ? scale : 1);
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
                uint16_t bits = (uint16_t)((g->bmp[j * 2] << 8) | g->bmp[j * 2 + 1]);
                for (int i = 0; i < 16; i++) {
                    if (bits & (1u << (15 - i))) {
                        epd1680_fb_pixel(s_fb, cx + i, y + j, 0);
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
                        epd1680_fb_pixel(s_fb, cx + i, y + j, 0);
                    }
                }
            }
        }
        cx += 10;
        p += n;
    }
    return cx - x;
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

/* 居中：给定像素宽算出左边界 */
static int cx_center(int w)
{
    int x = (EPD1680_WIDTH - w) / 2;
    return x < MARGIN ? MARGIN : x;
}

/* ---------------------------------------------------------------- 绘制页面 */

static void draw_status_bar(const char *left, const char *right)
{
    draw_ascii(MARGIN, Y_SBAR, left, 1);
    draw_ascii(EPD1680_WIDTH - MARGIN - ascii_w(right, 1), Y_SBAR, right, 1);
    epd1680_fb_hline(s_fb, MARGIN, Y_DIV1, INNER, 0);
}

static void draw_progress(int idx, int total)
{
    epd1680_fb_rect(s_fb, MARGIN, Y_BAR, INNER, BAR_H, 0);
    int fill_w = (INNER - 4) * (idx + 1) / total;
    if (fill_w > 0) {
        epd1680_fb_fill_rect(s_fb, MARGIN + 2, Y_BAR + 2, fill_w, BAR_H - 4, 0);
    }
}

static void draw_footer(void)
{
    draw_ascii(MARGIN, Y_HINT1, "PREV NEXT PLAY", 1);
    draw_ascii(MARGIN, Y_HINT2, "REC  SYNC HOLD", 1);
}

static void draw_meaning(const char *meaning)
{
    /* 每行 7 个汉字，最多两行 */
    char line[64];
    const char *p = meaning;
    int y = Y_MEAN;
    for (int row = 0; row < 2 && *p; row++) {
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
        text16(cx_center(cnt * 16), y, line);
        y += MEAN_LINE_H;
    }
}

static void draw_box(const char *big, const char *small)
{
    epd1680_fb_rect(s_fb, MARGIN, Y_BOX, INNER, BOX_H, 0);
    if (big && *big) {
        text16(cx_center(utf8_count(big) * 16), Y_BOX + 26, big);
    }
    if (small && *small) {
        text16(cx_center(utf8_count(small) * 16), Y_BOX + 56, small);
    }
}

static void draw_card_page(void)
{
    const ui29_word_t *w = &k_words29[s_idx];
    char idxbuf[16];
    snprintf(idxbuf, sizeof(idxbuf), "%02d/%02d", s_idx + 1, UI29_WORD_COUNT);

    draw_status_bar("UNIT 01", idxbuf);

    int ww = ascii_w(w->word, WORD_SCALE);
    draw_ascii(cx_center(ww), Y_WORD, w->word, WORD_SCALE);

    text12(cx_center(utf8_count(w->ipa) * 10), Y_IPA, w->ipa);
    epd1680_fb_hline(s_fb, MARGIN, Y_DIV2, INNER, 0);

    draw_meaning(w->meaning);
    draw_box(s_big, s_msg);
    draw_progress(s_idx, UI29_WORD_COUNT);
    draw_footer();
}

static void draw_home_page(void)
{
    draw_status_bar("E-INK WORD", "HOME");
    draw_ascii(cx_center(ascii_w("DAILY", 3)), 40, "DAILY", 3);
    text16(cx_center(4 * 16), 84, "单词学习");

    epd1680_fb_rect(s_fb, MARGIN, 120, INNER, 112, 0);
    const char *labels[3] = {"TOTAL", "TODAY", "STREAK"};
    char values[3][8];
    snprintf(values[0], sizeof(values[0]), "%d", UI29_WORD_COUNT);
    snprintf(values[1], sizeof(values[1]), "0");
    snprintf(values[2], sizeof(values[2]), "0");

    int y = 136;
    for (int i = 0; i < 3; i++) {
        draw_ascii(MARGIN + 10, y, labels[i], 1);
        draw_ascii(EPD1680_WIDTH - MARGIN - 10 - ascii_w(values[i], 1), y,
                   values[i], 1);
        /* 虚线分隔 */
        for (int x = MARGIN + 8; x < EPD1680_WIDTH - MARGIN - 8; x += 5) {
            epd1680_fb_hline(s_fb, x, y + 14, 3, 0);
        }
        y += 26;
    }

    draw_ascii(MARGIN, 250, "PLAY = START", 1);
    draw_ascii(MARGIN, 262, "SYNC = WIFI", 1);
    draw_ascii(MARGIN, 274, "HOLD NEXT = UNIT", 1);
}

static void draw_sync_page(void)
{
    draw_status_bar("SYNC", "WIFI");
    text16(cx_center(4 * 16), 40, "无线同步");

    epd1680_fb_rect(s_fb, MARGIN, 80, INNER, 92, 0);
    draw_ascii(MARGIN + 8, 92, "SSID", 1);
    draw_ascii(MARGIN + 8, 104, "EPD-Word-01", 1);
    draw_ascii(MARGIN + 8, 124, "URL", 1);
    draw_ascii(MARGIN + 8, 136, "192.168.4.1", 1);
    draw_ascii(MARGIN + 8, 150, "not enabled yet", 1);

    text16(cx_center(6 * 16), 200, "按同步键退出");
    draw_ascii(MARGIN, 250, "SYNC = STOP AP", 1);
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

/* ---------------------------------------------------------------- 主循环 */

void ui29_run(void)
{
    ESP_LOGI(TAG, "word card start: %d words", UI29_WORD_COUNT);

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
