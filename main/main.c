/*
 * E-Ink 英语学习机 —— 应用入口
 *
 * 硬件：ESP32-S3-N16R8 + 驱动板 V1.5 + 4.2" 单色墨水屏（SSD1619A，400x300）
 * 刷新：70 字节 partial LUT，400x300 全帧 transition-only，约 740 ms/次
 *
 * 当前行为（复刻已验证的 50 词演示，用于硬件回归验证）：
 *   1. epd_init() 建立 SPI/GPIO 并做 OTP 全刷初始化
 *   2. epd_partial_init() 硬件复位 + partial 初始化 + 加载 LUT
 *   3. 开机清洁（全白 -> 全黑 -> 底图）
 *   4. 50 个单词，每词约 740 ms 刷新 + 1 s 停留
 *   5. 第 25 词后清洁一次，跑完显示 DONE 并深睡
 *
 * 下一步（见 docs/交接文档.md 第 7 节）：
 *   - 改成按键翻词 + 按需刷新
 *   - 停留时间用 light sleep / tickless idle 替代 240 MHz 空转
 *   - 按 20~30 次刷新做自适应清洁
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "epd_ssd1619a.h"
#include "epd_lut_partial.h"
#include "word_view.h"

static const char *TAG = "words";

#define WORD_DWELL_MS      1000    /* 每词停留 */
#define CLEAN_EVERY_N      25      /* 每 N 次刷新做一次清洁周期 */
#define EPD_TIMEOUT_MS     5000    /* 单次刷新等待 BUSY 的超时 */

static uint8_t s_base[EPD_FRAME_BYTES];   /* 底图，只画一次 */
static uint8_t s_frame[EPD_FRAME_BYTES];  /* 当前待显示的完整帧 */

/* 全白 -> 全黑 -> 底图，恢复残影，并重新加载 partial LUT */
static void clean_cycle(const char *tag)
{
    int64_t t0 = esp_timer_get_time();
    epd_fb_clear(s_frame, 1);
    bool ok = epd_partial_show_full(s_frame, EPD_TIMEOUT_MS);
    ESP_LOGI(TAG, "clean %s white: %d, %lld ms", tag, ok ? 1 : 0,
             (long long)((esp_timer_get_time() - t0) / 1000));

    t0 = esp_timer_get_time();
    epd_fb_clear(s_frame, 0);
    ok = epd_partial_show_full(s_frame, EPD_TIMEOUT_MS);
    ESP_LOGI(TAG, "clean %s black: %d, %lld ms", tag, ok ? 1 : 0,
             (long long)((esp_timer_get_time() - t0) / 1000));

    t0 = esp_timer_get_time();
    word_view_base(s_base);
    ok = epd_partial_show_full(s_base, EPD_TIMEOUT_MS);
    ESP_LOGI(TAG, "clean %s base: %d, %lld ms", tag, ok ? 1 : 0,
             (long long)((esp_timer_get_time() - t0) / 1000));

    ok = epd_partial_reload_lut(epd_lut_partial_70);
    ESP_LOGI(TAG, "clean %s lut: %d", tag, ok ? 1 : 0);
}

/* 合成第 idx 张单词卡并全帧刷新 */
static bool show_word(int idx)
{
    word_view_card(s_frame, s_base, idx);

    int64_t t0 = esp_timer_get_time();
    bool ok = epd_partial_update(s_frame, 0, 0, EPD_WIDTH, EPD_HEIGHT, EPD_TIMEOUT_MS);
    int64_t dt = (esp_timer_get_time() - t0) / 1000;

    ESP_LOGI(TAG, "%02d/%d %-5s: %d, %lld ms", idx + 1, word_view_count(),
             word_view_word(idx), ok ? 1 : 0, (long long)dt);
    return ok;
}

void app_main(void)
{
    ESP_LOGI(TAG, "start, %d words", word_view_count());

    const epd_config_t cfg = EPD_S3_DEFAULT_CONFIG();
    epd_init(&cfg);

    if (!epd_partial_init(epd_lut_partial_70)) {
        ESP_LOGE(TAG, "partial init failed");
        return;
    }

    /* 清掉上一轮残留，再开始 */
    clean_cycle("START");

    for (int i = 0; i < word_view_count(); i++) {
        show_word(i);
        vTaskDelay(pdMS_TO_TICKS(WORD_DWELL_MS));

        /* 每 N 次刷新清洁一次；最后一词后由 END 统一清洁，避免重复 */
        if ((i + 1) % CLEAN_EVERY_N == 0 && i + 1 < word_view_count()) {
            clean_cycle("MID");
        }
    }

    clean_cycle("END");

    /* 收尾：底图 + DONE，然后深睡（画面保持） */
    word_view_base(s_base);
    memcpy(s_frame, s_base, sizeof(s_frame));
    word_view_banner(s_frame, "DONE");
    epd_partial_show_full(s_frame, EPD_TIMEOUT_MS);

    ESP_LOGI(TAG, "done");
    epd_deep_sleep();
}
