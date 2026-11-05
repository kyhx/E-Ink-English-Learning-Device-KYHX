/*
 * panel1680_test.c
 *
 * 2.9" SSD1680 (ZJY128296-029EAAMFGN) 上电自检：
 *   1) 初始化 + BUSY 检查
 *   2) 全白 / 全黑 全刷（0xF7）
 *   3) 图案页全刷（边框 + 标题 + 文本 + 棋盘）
 *   4) 矩形局刷两帧（同一窗口改文字，验证真局刷）
 *   5) 整屏局刷（0xFF，468 ms）计时
 *   6) 输出可直接抄进测试记录的结果行
 *
 * 判定要点（人眼看屏）：
 *   - 全白页应为纯白，无灰底；全黑页应为纯黑，无白点
 *   - 棋盘格应为清晰黑白方块，无粘连
 *   - 局刷两帧后，窗口外区域不应有任何变化
 *   - 局刷单次耗时应明显小于全刷
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "sdkconfig.h"

#include "epd_ssd1680.h"

static const char *TAG = "p1680_test";

static uint8_t s_fb[EPD1680_FRAME_BYTES];
static uint8_t s_win[EPD1680_FRAME_BYTES];

#define TMO_MS 8000

static int64_t us(void)
{
    return esp_timer_get_time();
}

/* 图案页：边框 + 标题 + 说明行 + 棋盘 + 局刷窗口区 */
static void draw_pattern(void)
{
    epd1680_fb_clear(s_fb, 1);

    /* 双线边框 */
    epd1680_fb_rect(s_fb, 0, 0, EPD1680_WIDTH, EPD1680_HEIGHT, 0);
    epd1680_fb_rect(s_fb, 2, 2, EPD1680_WIDTH - 4, EPD1680_HEIGHT - 4, 0);

    epd1680_fb_text_scaled(s_fb, 8, 12, "SSD1680", 0, 2);
    epd1680_fb_text(s_fb, 8, 40, "2.9in 128x296", 0);
    epd1680_fb_text(s_fb, 8, 52, "ZJY128296-029", 0);
    epd1680_fb_text(s_fb, 8, 64, "B/W 1bpp SPI", 0);

    /* 水平线 + 竖线，检查像素对齐 */
    epd1680_fb_hline(s_fb, 6, 80, EPD1680_WIDTH - 12, 0);
    epd1680_fb_vline(s_fb, EPD1680_WIDTH / 2, 84, 24, 0);

    /* 棋盘 4x4 像素块，检查点阵与拖影 */
    for (int y = 116; y < 176; y += 4) {
        for (int x = 8; x < 120; x += 4) {
            if (((x / 4) + (y / 4)) & 1) {
                epd1680_fb_fill_rect(s_fb, x, y, 4, 4, 0);
            }
        }
    }

    /* 局刷演示窗口：这里先画好「第 0 帧」的内容 */
    epd1680_fb_fill_rect(s_fb, 0, 200, EPD1680_WIDTH, 32, 1);
    epd1680_fb_hline(s_fb, 0, 200, EPD1680_WIDTH, 0);
    epd1680_fb_hline(s_fb, 0, 231, EPD1680_WIDTH, 0);
    epd1680_fb_text_scaled(s_fb, 8, 208, "PARTIAL 0", 0, 2);

    epd1680_fb_text(s_fb, 8, 248, "1=white 0=black", 0);
    epd1680_fb_text(s_fb, 8, 262, "RAM 100..15F", 0);
    epd1680_fb_text(s_fb, 8, 276, "if inverted: ctrl", 0);
}

/* 生成局刷窗口内容：白底 + 上下两条黑边（用于确认窗口边界）+ 一行放大文字
 * x/w 必须是 8 的倍数。 */
static void build_window(int x, int y, int w, int h, const char *text)
{
    (void)x;
    (void)y;
    size_t len = (size_t)(w / 8) * (size_t)h;
    memset(s_win, 0xFF, len); /* 1 = 白 */

    /* 窗口上下边界线 */
    for (int i = 0; i < w; i++) {
        size_t idx_top = (size_t)(0 * (w / 8) + (i / 8));
        size_t idx_bot = (size_t)((h - 1) * (w / 8) + (i / 8));
        uint8_t mask = (uint8_t)(0x80 >> (i & 7));
        s_win[idx_top] &= (uint8_t)~mask;
        s_win[idx_bot] &= (uint8_t)~mask;
    }

    if (text) {
        static uint8_t tmp[EPD1680_FRAME_BYTES];
        epd1680_fb_clear(tmp, 1);
        epd1680_fb_text_scaled(tmp, 0, 8, text, 0, 2);
        for (int j = 8; j < h - 1 && j < 24; j++) {
            for (int i = 0; i < w; i++) {
                uint8_t bit = (tmp[j * EPD1680_BYTES_PER_ROW + (i >> 3)] >>
                               (7 - (i & 7))) & 1;
                size_t idx = (size_t)(j * (w / 8) + (i / 8));
                uint8_t mask = (uint8_t)(0x80 >> (i & 7));
                if (bit) {
                    s_win[idx] |= mask;
                } else {
                    s_win[idx] &= (uint8_t)~mask;
                }
            }
        }
    }
}

void panel1680_test_run(void)
{
    ESP_LOGI(TAG, "== ZJY128296-029EAAMFGN / SSD1680 self test ==");

    int64_t t0 = us();
    bool ok = epd1680_init(NULL);
    int64_t t_init = (us() - t0) / 1000;
    ESP_LOGI(TAG, "init: ok=%d %lld ms busy_level=%d", ok, (long long)t_init,
             epd1680_busy_level());
    if (!ok) {
        ESP_LOGE(TAG, "init failed: check wiring / 3.3V / BUSY polarity");
        return;
    }

    /* 1) 全白 */
    epd1680_fb_clear(s_fb, 1);
    t0 = us();
    ok = epd1680_full(s_fb, TMO_MS);
    int64_t t_white = (us() - t0) / 1000;
    ESP_LOGI(TAG, "full_white: ok=%d %lld ms", ok, (long long)t_white);
    vTaskDelay(pdMS_TO_TICKS(800));

    /* 2) 全黑 */
    epd1680_fb_clear(s_fb, 0);
    t0 = us();
    ok = epd1680_full(s_fb, TMO_MS);
    int64_t t_black = (us() - t0) / 1000;
    ESP_LOGI(TAG, "full_black: ok=%d %lld ms", ok, (long long)t_black);
    vTaskDelay(pdMS_TO_TICKS(800));

    /* 3) 图案页（同时建立局刷基准） */
    draw_pattern();
    t0 = us();
    ok = epd1680_full(s_fb, TMO_MS);
    int64_t t_pat = (us() - t0) / 1000;
    ESP_LOGI(TAG, "full_pattern: ok=%d %lld ms", ok, (long long)t_pat);
    vTaskDelay(pdMS_TO_TICKS(1200));

    /* 4) 矩形局刷：同一窗口改两帧 */
    const int wx = 0, wy = 200, ww = EPD1680_WIDTH, wh = 32;
    int64_t t_p1 = 0, t_p2 = 0;
    bool p1 = false, p2 = false;

    build_window(wx, wy, ww, wh, "PARTIAL 1");
    t0 = us();
    p1 = epd1680_partial(s_win, wx, wy, ww, wh, TMO_MS);
    t_p1 = (us() - t0) / 1000;
    ESP_LOGI(TAG, "partial_1: ok=%d %lld ms (window %d,%d %dx%d)", p1,
             (long long)t_p1, wx, wy, ww, wh);
    vTaskDelay(pdMS_TO_TICKS(1500));

    build_window(wx, wy, ww, wh, "PARTIAL 2");
    t0 = us();
    p2 = epd1680_partial(s_win, wx, wy, ww, wh, TMO_MS);
    t_p2 = (us() - t0) / 1000;
    ESP_LOGI(TAG, "partial_2: ok=%d %lld ms", p2, (long long)t_p2);
    vTaskDelay(pdMS_TO_TICKS(1500));

    /* 5) 整屏局刷（相当于全屏快刷） */
    draw_pattern();
    t0 = us();
    ok = epd1680_fast(s_fb, TMO_MS);
    int64_t t_fast = (us() - t0) / 1000;
    ESP_LOGI(TAG, "fast_full: ok=%d %lld ms", ok, (long long)t_fast);

    /* 6) 汇总：一行一条，方便抄进 docs/SSD1680测试记录.md */
    ESP_LOGI(TAG, "---- summary (copy to test record) ----");
    ESP_LOGI(TAG, "panel=SSD1680 128x296 module=ZJY128296-029EAAMFGN");
    ESP_LOGI(TAG, "init_ms=%lld", (long long)t_init);
    ESP_LOGI(TAG, "full_white_ms=%lld full_black_ms=%lld full_pattern_ms=%lld",
             (long long)t_white, (long long)t_black, (long long)t_pat);
    ESP_LOGI(TAG, "partial_ms=%lld/%lld partial_ok=%d/%d window=%dx%d",
             (long long)t_p1, (long long)t_p2, p1, p2, ww, wh);
    ESP_LOGI(TAG, "fast_full_ms=%lld", (long long)t_fast);
    ESP_LOGI(TAG, "---------------------------------------");
    ESP_LOGI(TAG, "keep image on screen; use `bench epd1680` for more tests");
}
