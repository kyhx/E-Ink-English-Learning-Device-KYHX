/*
 * app_state.c — M0 home screen + 5-key event loop.
 *
 * This is deliberately small: it proves the display, key driver and bench
 * console coexist.  M1 will replace the text-only home screen with the real
 * page manager / word card.
 *
 * SPDX-License-Identifier: MIT
 */
#include "app_state.h"

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_timer.h"

#include "buttons.h"
#include "epd_ssd1619a.h"

static const char *TAG = "app";

#define HOME_BAND_Y0 95
#define HOME_BAND_Y1 164

static uint8_t s_base[EPD_FRAME_BYTES];
static uint8_t s_frame[EPD_FRAME_BYTES];

static void draw_centered_scaled(uint8_t *fb, int y, const char *text, int scale)
{
    int w = epd_fb_text_width_scaled(text, scale);
    epd_fb_text_scaled(fb, (EPD_WIDTH - w) / 2, y, text, 0, scale);
}

static void render_home_base(void)
{
    epd_fb_clear(s_base, 1);
    epd_fb_rect(s_base, 0, 0, EPD_WIDTH, EPD_HEIGHT, 0);

    draw_centered_scaled(s_base, 30, "E-INK WORDS", 3);
    draw_centered_scaled(s_base, 185, "M0 / BENCH", 2);
    draw_centered_scaled(s_base, 265, "bench> help", 1);
}

static void show_band_text(const char *text, int scale)
{
    memcpy(s_frame, s_base, EPD_FRAME_BYTES);
    int w = epd_fb_text_width_scaled(text, scale);
    int y = HOME_BAND_Y0 + (HOME_BAND_Y1 - HOME_BAND_Y0 + 1 - 8 * scale) / 2;
    epd_fb_text_scaled(s_frame, (EPD_WIDTH - w) / 2, y, text, 0, scale);
    epd_band_update(s_frame, HOME_BAND_Y0, HOME_BAND_Y1, 5000);
}

void app_state_run(void)
{
    render_home_base();
    memcpy(s_frame, s_base, EPD_FRAME_BYTES);
    show_band_text("READY", 4);

    ESP_LOGI(TAG, "M0 app ready; type 'bench help' on UART0 (115200)");

    uint32_t count = 0;
    while (true) {
        uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
        button_event_t ev = buttons_poll(now_ms);

        if (ev != BUTTON_EVENT_NONE) {
            count++;
            ESP_LOGI(TAG, "event #%u: %s (mask=0x%02x)",
                     (unsigned)count, buttons_event_name(ev), buttons_mask());

            char line[32];
            snprintf(line, sizeof(line), "%s", buttons_event_name(ev));
            show_band_text(line, 3);
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
