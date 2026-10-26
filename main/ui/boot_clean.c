/*
 * boot_clean.c — white -> black -> base full refresh on cold boot.
 *
 * 三步都用 partial LUT 整屏刷新（实测约 709 ms/步），不是 346 ms 的带状
 * 局刷。白/黑把上一轮残影推到饱和，再回到底图，最后重载 70 行 LUT。
 *
 * SPDX-License-Identifier: MIT
 */
#include "boot_clean.h"

#include <string.h>

#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"

#include "epd_lut_partial.h"
#include "epd_ssd1619a.h"

static const char *TAG = "boot_clean";

bool boot_clean_should_run(void)
{
    esp_reset_reason_t reason = esp_reset_reason();
    return reason == ESP_RST_POWERON || reason == ESP_RST_EXT ||
           reason == ESP_RST_BROWNOUT;
}

bool boot_clean_run(uint8_t *base, uint8_t *frame, const char *tag)
{
    if (base == NULL || frame == NULL) {
        return false;
    }

    int64_t t_all = esp_timer_get_time();
    int64_t t0 = t_all;
    bool ok = true;

    epd_fb_clear(frame, 1);
    bool step = epd_partial_show_full(frame, 5000);
    ok = ok && step;
    int64_t t1 = esp_timer_get_time();
    ESP_LOGI(TAG, "%s white: %s %lld ms", tag, step ? "ok" : "FAIL",
             (long long)((t1 - t0) / 1000));

    epd_fb_clear(frame, 0);
    step = epd_partial_show_full(frame, 5000);
    ok = ok && step;
    int64_t t2 = esp_timer_get_time();
    ESP_LOGI(TAG, "%s black: %s %lld ms", tag, step ? "ok" : "FAIL",
             (long long)((t2 - t1) / 1000));

    memcpy(frame, base, EPD_FRAME_BYTES);
    step = epd_partial_show_full(frame, 5000);
    ok = ok && step;
    int64_t t3 = esp_timer_get_time();
    ESP_LOGI(TAG, "%s base:  %s %lld ms", tag, step ? "ok" : "FAIL",
             (long long)((t3 - t2) / 1000));

    step = epd_partial_reload_lut(epd_lut_partial_70);
    ok = ok && step;
    ESP_LOGI(TAG, "%s LUT:   %s; total=%lld ms", tag, step ? "ok" : "FAIL",
             (long long)((esp_timer_get_time() - t_all) / 1000));
    return ok;
}
