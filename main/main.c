/*
 * E-Ink 英语学习机 —— 应用入口（M2）
 *
 * 硬件：ESP32-S3-N16R8 + 驱动板 V1.5 + 4.2" 单色墨水屏（SSD1619A，400x300）
 *
 * M2 启动流程：
 *   1. NVS + EPD 初始化（partial LUT）
 *   2. LittleFS 挂载 10 MB storage 分区（首次自动格式化）
 *   3. M2 音频链（I2S0 麦克风 / I2S1 功放 / WAV / VAD）
 *   4. 5 键驱动 + UART0 bench 控制台
 *   5. CONFIG_APP_MODE_DEMO=y 时跑旧 50 词演示；否则进入 M2 录音页
 *
 * bench 命令：
 *   bench info / fs / display band <y0> <h> <n> / display full /
 *   display clean / keys [s] / selftest
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

#include "epd_lut_partial.h"
#include "epd_ssd1619a.h"

#include "app_state.h"
#include "audio.h"
#include "bench.h"
#include "board/buttons.h"
#include "services/storage.h"

static const char *TAG = "main";

#define EPD_TIMEOUT_MS 5000

/* --------------------------------------------------------------- NVS */
static esp_err_t nvs_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS needs erase (%s), re-initializing", esp_err_to_name(err));
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    return err;
}

/* ------------------------------------------------------- legacy demo */
#if CONFIG_APP_MODE_DEMO

#include "word_view.h"

#define WORD_DWELL_MS  1000
#define CLEAN_EVERY_N  25

static uint8_t s_base[EPD_FRAME_BYTES];
static uint8_t s_frame[EPD_FRAME_BYTES];

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

/* 合成第 idx 张单词卡并做 70 行带状局刷 */
static bool show_word(int idx)
{
    word_view_card(s_frame, s_base, idx);

    int64_t t0 = esp_timer_get_time();
    bool ok = epd_band_update(s_frame, 95, 164, EPD_TIMEOUT_MS);
    int64_t dt = (esp_timer_get_time() - t0) / 1000;

    ESP_LOGI(TAG, "%02d/%d %-5s: %d, %lld ms", idx + 1, word_view_count(),
             word_view_word(idx), ok ? 1 : 0, (long long)dt);
    return ok;
}

static void demo_run(void)
{
    ESP_LOGI(TAG, "demo mode: %d words", word_view_count());

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

    ESP_LOGI(TAG, "demo done");
    epd_deep_sleep();
}

#endif /* CONFIG_APP_MODE_DEMO */

/* ------------------------------------------------------------- entry */
void app_main(void)
{
    ESP_LOGI(TAG, "boot: M2 audio chain");

    esp_err_t err = nvs_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs init failed: %s", esp_err_to_name(err));
    }

    const epd_config_t cfg = EPD_S3_DEFAULT_CONFIG();
    epd_init(&cfg);

    if (!epd_partial_init(epd_lut_partial_70)) {
        ESP_LOGE(TAG, "partial init failed; display unavailable");
        return;
    }

    err = storage_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "storage init failed: %s (bench fs will fail)",
                 esp_err_to_name(err));
    }

    err = audio_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "audio init failed: %s (bench audio will fail)",
                 esp_err_to_name(err));
    }

    err = buttons_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "buttons init failed: %s", esp_err_to_name(err));
    }

#if CONFIG_APP_BENCH
    err = bench_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "bench init failed: %s", esp_err_to_name(err));
    }
#endif

#if CONFIG_APP_MODE_DEMO
    demo_run();
#else
    app_state_run();  /* does not return */
#endif
}
