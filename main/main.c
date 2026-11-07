/*
 * E-Ink 英语学习机 —— 应用入口（2.9" 单屏精简版）
 *
 * 硬件：ESP32-S3-WROOM-1-N16R8 + 2.9" SSD1680（ZJY128296-029EAAMFGN，128x296）
 *
 * 启动流程：
 *   1. NVS
 *   2. SSD1680 初始化（失败则退到面板自检，方便看接线问题）
 *   3. LittleFS 挂载 10 MB storage 分区（首次自动格式化）
 *   4. 音频链（I2S0 麦克风 / I2S1 功放 / WAV / VAD）
 *   5. 5 键驱动 + UART0 bench 控制台
 *   6. 进入单词卡 UI（主菜单 → 单词/复习日历/同步/设置）
 *
 * 这个分支只保留 2.9" 单屏路径：4.2" SSD1619A 驱动、带状局刷、
 * 采集 UI、50 词 demo 都已移除。双屏对照数据见 docs/ 里的测试记录。
 *
 * bench 命令：
 *   bench info / fs / keys / audio ... / epd1680 ... / ui page|flip
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

#include "epd_ssd1680.h"

#include "audio.h"
#include "bench.h"
#include "board/buttons.h"
#include "panel1680_test.h"
#include "services/storage.h"
#include "ui29_card.h"

static const char *TAG = "main";

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

/* ------------------------------------------------------------- entry */
void app_main(void)
{
    ESP_LOGI(TAG, "boot: 2.9in single-panel build");

    esp_err_t err = nvs_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs init failed: %s", esp_err_to_name(err));
    }

    if (!epd1680_init(NULL)) {
        ESP_LOGE(TAG, "SSD1680 init failed; running panel self test");
        panel1680_test_run();
        for (;;) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
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

    ui29_run();       /* does not return */
}
