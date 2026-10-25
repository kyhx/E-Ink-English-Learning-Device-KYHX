/*
 * bench.c — serial console commands for M0 bring-up.
 *
 * Runs in its own REPL task; display commands must not be called while the
 * app is in the middle of a refresh (M0 has no display mutex yet).
 *
 * SPDX-License-Identifier: MIT
 */
#include "bench.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_chip_info.h"
#include "esp_console.h"
#include "esp_heap_caps.h"
#include "esp_idf_version.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"

#include "buttons.h"
#include "epd_lut_partial.h"
#include "epd_ssd1619a.h"
#include "storage.h"

static const char *TAG = "bench";

/* --------------------------------------------------------------- helpers */

static const char *reset_reason_name(esp_reset_reason_t r)
{
    switch (r) {
    case ESP_RST_POWERON:  return "POWERON";
    case ESP_RST_EXT:      return "EXT";
    case ESP_RST_SW:       return "SW";
    case ESP_RST_PANIC:    return "PANIC";
    case ESP_RST_INT_WDT:  return "INT_WDT";
    case ESP_RST_TASK_WDT: return "TASK_WDT";
    case ESP_RST_WDT:      return "WDT";
    case ESP_RST_DEEPSLEEP:return "DEEPSLEEP";
    case ESP_RST_BROWNOUT: return "BROWNOUT";
    case ESP_RST_SDIO:     return "SDIO";
    default:               return "UNKNOWN";
    }
}

static void draw_centered(uint8_t *fb, int y, const char *text, int scale)
{
    int w = epd_fb_text_width_scaled(text, scale);
    epd_fb_text_scaled(fb, (EPD_WIDTH - w) / 2, y, text, 0, scale);
}

static void draw_bench_base(uint8_t *fb)
{
    epd_fb_clear(fb, 1);
    epd_fb_rect(fb, 0, 0, EPD_WIDTH, EPD_HEIGHT, 0);
    draw_centered(fb, 30, "BENCH", 3);
    draw_centered(fb, 185, "M0 / EPD + FS + KEYS", 2);
    draw_centered(fb, 265, "bench> help", 1);
}

static int cmd_bench_info(void)
{
    esp_chip_info_t chip;
    esp_chip_info(&chip);

    printf("\n== bench info ==\n");
    printf("chip: ESP32-S3, cores=%d, revision=%d\n", chip.cores, chip.revision);
    printf("IDF: %s\n", esp_get_idf_version());
    printf("uptime: %lld s\n", (long long)(esp_timer_get_time() / 1000000));
    printf("reset: %s (%d)\n", reset_reason_name(esp_reset_reason()),
           (int)esp_reset_reason());
    printf("heap internal: free=%u total=%u\n",
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
           (unsigned)heap_caps_get_total_size(MALLOC_CAP_INTERNAL));
    printf("heap PSRAM:    free=%u total=%u\n",
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
           (unsigned)heap_caps_get_total_size(MALLOC_CAP_SPIRAM));
    printf("storage: %s\n", storage_is_mounted() ? "mounted" : "NOT mounted");
    return 0;
}

static int cmd_bench_fs(void)
{
    printf("\n== bench fs ==\n");
    esp_err_t a = storage_info();
    esp_err_t b = storage_selftest();
    printf("fs: info=%s selftest=%s\n", esp_err_to_name(a), esp_err_to_name(b));
    return (a == ESP_OK && b == ESP_OK) ? 0 : 1;
}

static int cmd_bench_display_band(int y0, int h, int n)
{
    if (y0 < 0 || h <= 0 || y0 + h > EPD_HEIGHT || n <= 0 || n > 100) {
        printf("usage: bench display band <y0> <h> <n>  (0<=y0, y0+h<=300, 1<=n<=100)\n");
        return 1;
    }

    static uint8_t fb[EPD_FRAME_BYTES];
    draw_bench_base(fb);

    int64_t min_us = INT64_MAX;
    int64_t max_us = 0;
    int64_t sum_us = 0;
    bool all_ok = true;

    printf("\n== band refresh y=%d h=%d n=%d ==\n", y0, h, n);
    for (int i = 0; i < n; i++) {
        char line[24];
        snprintf(line, sizeof(line), "BAND %d", i);
        draw_bench_base(fb);  /* clear previous text inside the band */
        draw_centered(fb, y0 + (h - 8 * 4) / 2, line, 4);

        int64_t t0 = esp_timer_get_time();
        bool ok = epd_band_update(fb, y0, y0 + h - 1, 5000);
        int64_t dt = esp_timer_get_time() - t0;
        if (dt < min_us) min_us = dt;
        if (dt > max_us) max_us = dt;
        sum_us += dt;
        all_ok = all_ok && ok;
        printf("  #%02d %s %lld ms\n", i, ok ? "ok" : "FAIL", (long long)(dt / 1000));
    }

    printf("band: min=%lld ms avg=%lld ms max=%lld ms\n",
           (long long)(min_us / 1000), (long long)(sum_us / n / 1000),
           (long long)(max_us / 1000));
    return all_ok ? 0 : 1;
}

static int cmd_bench_display_full(void)
{
    static uint8_t fb[EPD_FRAME_BYTES];
    draw_bench_base(fb);
    int64_t t0 = esp_timer_get_time();
    bool ok = epd_partial_show_full(fb, 5000);
    int64_t dt = esp_timer_get_time() - t0;
    printf("\nfull refresh: %s %lld ms\n", ok ? "ok" : "FAIL", (long long)(dt / 1000));
    return ok ? 0 : 1;
}

static int cmd_bench_display_clean(void)
{
    static uint8_t fb[EPD_FRAME_BYTES];
    printf("\n== clean cycle ==\n");

    bool all_ok = true;
    int64_t t0 = esp_timer_get_time();
    epd_fb_clear(fb, 1);
    bool ok = epd_partial_show_full(fb, 5000);
    all_ok = all_ok && ok;
    printf("  white: %s %lld ms\n", ok ? "ok" : "FAIL",
           (long long)((esp_timer_get_time() - t0) / 1000));

    t0 = esp_timer_get_time();
    epd_fb_clear(fb, 0);
    ok = epd_partial_show_full(fb, 5000);
    all_ok = all_ok && ok;
    printf("  black: %s %lld ms\n", ok ? "ok" : "FAIL",
           (long long)((esp_timer_get_time() - t0) / 1000));

    t0 = esp_timer_get_time();
    draw_bench_base(fb);
    ok = epd_partial_show_full(fb, 5000);
    all_ok = all_ok && ok;
    printf("  base:  %s %lld ms\n", ok ? "ok" : "FAIL",
           (long long)((esp_timer_get_time() - t0) / 1000));

    bool lut = epd_partial_reload_lut(epd_lut_partial_70);
    all_ok = all_ok && lut;
    printf("  LUT reload: %s\n", lut ? "ok" : "FAIL");
    return all_ok ? 0 : 1;
}

static int cmd_bench_keys(int seconds)
{
    if (seconds < 1 || seconds > 30) {
        printf("usage: bench keys [seconds]  (1..30)\n");
        return 1;
    }
    printf("\n== keys: press for %d s ==\n", seconds);
    printf("start mask=0x%02x (bit0=PREV bit1=NEXT bit2=PLAY bit3=RECORD bit4=SYNC)\n",
           buttons_mask());
    int64_t end = esp_timer_get_time() + (int64_t)seconds * 1000000;
    while (esp_timer_get_time() < end) {
        uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
        button_event_t ev = buttons_poll(now_ms);
        if (ev != BUTTON_EVENT_NONE) {
            printf("  key: %-12s mask=0x%02x\n", buttons_event_name(ev), buttons_mask());
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    printf("keys: done\n");
    return 0;
}

static int cmd_bench_selftest(void)
{
    printf("\n===== bench selftest =====\n");
    int rc = 0;
    rc |= cmd_bench_info();
    rc |= cmd_bench_fs();
    rc |= cmd_bench_display_band(95, 70, 3);
    rc |= cmd_bench_display_full();
    printf("===== selftest: %s =====\n", rc == 0 ? "PASS" : "FAIL");
    return rc;
}

static void print_bench_usage(void)
{
    printf("usage:\n");
    printf("  bench info\n");
    printf("  bench fs\n");
    printf("  bench display band <y0> <h> <n>\n");
    printf("  bench display full\n");
    printf("  bench display clean\n");
    printf("  bench keys [seconds]\n");
    printf("  bench selftest\n");
}

static int cmd_bench(int argc, char **argv)
{
    if (argc < 2) {
        print_bench_usage();
        return 0;
    }

    if (!strcmp(argv[1], "info")) {
        return cmd_bench_info();
    }
    if (!strcmp(argv[1], "fs")) {
        return cmd_bench_fs();
    }
    if (!strcmp(argv[1], "keys")) {
        int sec = (argc >= 3) ? atoi(argv[2]) : 5;
        return cmd_bench_keys(sec);
    }
    if (!strcmp(argv[1], "selftest")) {
        return cmd_bench_selftest();
    }
    if (!strcmp(argv[1], "display")) {
        if (argc < 3) {
            print_bench_usage();
            return 1;
        }
        if (!strcmp(argv[2], "band")) {
            if (argc < 6) {
                print_bench_usage();
                return 1;
            }
            return cmd_bench_display_band(atoi(argv[3]), atoi(argv[4]), atoi(argv[5]));
        }
        if (!strcmp(argv[2], "full")) {
            return cmd_bench_display_full();
        }
        if (!strcmp(argv[2], "clean")) {
            return cmd_bench_display_clean();
        }
        printf("unknown display subcommand: %s\n", argv[2]);
        return 1;
    }

    printf("unknown bench subcommand: %s\n", argv[1]);
    print_bench_usage();
    return 1;
}

esp_err_t bench_init(void)
{
    esp_console_repl_t *repl = NULL;

    esp_console_repl_config_t repl_cfg = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_cfg.prompt = "bench>";
    repl_cfg.max_cmdline_length = 128;

    esp_console_dev_uart_config_t uart_cfg = {
        .channel = 0,
        .baud_rate = 115200,
        .tx_gpio_num = -1,
        .rx_gpio_num = -1,
    };

    esp_err_t err = esp_console_new_repl_uart(&uart_cfg, &repl_cfg, &repl);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_console_new_repl_uart failed: %s", esp_err_to_name(err));
        return err;
    }

    const esp_console_cmd_t cmd = {
        .command = "bench",
        .help = "E-Ink board bring-up commands",
        .hint = "info | fs | display | keys | selftest",
        .func = cmd_bench,
    };
    err = esp_console_cmd_register(&cmd);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "register bench failed: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_console_register_help_command();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "register help failed: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_console_start_repl(repl);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "start repl failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "console started on UART0 115200; type 'bench help'");
    return ESP_OK;
}
