/*
 * audio_in.c — I2S0 RX capture task for the MS4030 (INMP441 compatible).
 *
 * SPDX-License-Identifier: MIT
 */
#include "audio_in.h"

#include <inttypes.h>
#include <math.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "sdkconfig.h"

#include "audio_common.h"
#include "audio_out.h"
#include "board_pins.h"

static const char *TAG = "audio_in";

#define REC_CHUNK_FRAMES 160       /* 10 ms at 16 kHz */
#define REC_TASK_STACK   4096
#define SETTLE_SAMPLES   1600   /* 100 ms: MS4030 power-on / DC settling */

typedef struct {
    i2s_chan_handle_t rx;
    SemaphoreHandle_t lock;
    SemaphoreHandle_t done;
    int16_t *pcm;
    size_t cap;
    size_t count;

    volatile bool active;
    volatile bool done_flag;
    volatile bool stop_req;
    volatile int level_permille;
    volatile uint32_t elapsed_ms;

    int64_t start_us;
    int64_t first_us;
    uint32_t requested_ms;
    audio_capture_stats_t stats;
} audio_in_ctx_t;

static audio_in_ctx_t s_in;
static bool s_inited;

static void mic_power(bool on)
{
#if CONFIG_APP_AUDIO_MIC_POWER_GPIO >= 0
    gpio_set_level((gpio_num_t)CONFIG_APP_AUDIO_MIC_POWER_GPIO, on ? 1 : 0);
#else
    (void)on;
#endif
}

static void fill_stats(void)
{
    size_t n = s_in.count;
    if (n == 0) {
        return;
    }

    int64_t sum = 0;
    int32_t peak = 0;
    uint32_t clipped = 0;
    for (size_t i = 0; i < n; i++) {
        int32_t v = s_in.pcm[i];
        sum += v;
        int32_t a = v < 0 ? -v : v;
        if (a > peak) {
            peak = a;
        }
        if (a >= 32700) {
            clipped++;
        }
    }
    int32_t dc = (int32_t)(sum / (int64_t)n);

    double energy = 0.0;
    for (size_t i = 0; i < n; i++) {
        double v = (double)s_in.pcm[i] - dc;
        energy += v * v;
    }

    s_in.stats.requested_ms = s_in.requested_ms;
    s_in.stats.captured_ms = (uint32_t)((uint64_t)n * 1000 / AUDIO_SAMPLE_RATE);
    s_in.stats.start_latency_ms = (uint32_t)((s_in.first_us - s_in.start_us) / 1000);
    s_in.stats.peak = (uint16_t)(peak > 65535 ? 65535 : peak);
    s_in.stats.rms = (uint16_t)sqrt(energy / (double)n);
    s_in.stats.dc = (int16_t)dc;
    s_in.stats.clipped = clipped;
}

static void audio_in_task(void *arg)
{
    (void)arg;
    int32_t raw[REC_CHUNK_FRAMES];
    size_t target = (size_t)((uint64_t)s_in.requested_ms * AUDIO_SAMPLE_RATE / 1000);
    if (target > s_in.cap) {
        target = s_in.cap;
    }
    size_t settle = SETTLE_SAMPLES;

    while (!s_in.stop_req && s_in.count < target) {
        size_t bytes = 0;
        esp_err_t err = i2s_channel_read(s_in.rx, raw, sizeof(raw), &bytes,
                                         pdMS_TO_TICKS(200));
        if (err == ESP_ERR_TIMEOUT) {
            continue;
        }
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "i2s read failed: %s", esp_err_to_name(err));
            s_in.stats.read_errors++;
            break;
        }
        if (bytes == 0) {
            vTaskDelay(pdMS_TO_TICKS(1));
            continue;
        }
        size_t got = bytes / sizeof(int32_t);
        int peak = 0;
        for (size_t i = 0; i < got && s_in.count < target; i++) {
            if (settle > 0) {
                settle--;
                continue;
            }
            if (s_in.first_us == 0) {
                s_in.first_us = esp_timer_get_time();
            }
            /* INMP441: 24 valid bits, MSB first, left aligned in a 32-bit
             * slot.  Taking bits 31..16 gives the top 16 bits. */
            int32_t v = raw[i] >> 16;
            v = (v * CONFIG_APP_AUDIO_MIC_GAIN_X100) / 100;
            if (v > 32767) {
                v = 32767;
            } else if (v < -32768) {
                v = -32768;
            }
            s_in.pcm[s_in.count++] = (int16_t)v;
            int a = v < 0 ? -v : v;
            if (a > peak) {
                peak = a;
            }
        }
        s_in.level_permille = peak * 1000 / 32768;
        s_in.elapsed_ms = (uint32_t)((uint64_t)s_in.count * 1000 /
                                     AUDIO_SAMPLE_RATE);
    }

    i2s_channel_disable(s_in.rx);
    mic_power(false);
    fill_stats();

    s_in.done_flag = true;
    s_in.level_permille = 0;
    xSemaphoreGive(s_in.done);
    vTaskDelete(NULL);
}

esp_err_t audio_in_init(void)
{
    if (s_inited) {
        return ESP_OK;
    }

    s_in.lock = xSemaphoreCreateMutex();
    s_in.done = xSemaphoreCreateBinary();
    if (s_in.lock == NULL || s_in.done == NULL) {
        return ESP_ERR_NO_MEM;
    }

#if CONFIG_APP_AUDIO_MIC_POWER_GPIO >= 0
    gpio_config_t pwr = {
        .pin_bit_mask = 1ULL << CONFIG_APP_AUDIO_MIC_POWER_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t perr = gpio_config(&pwr);
    if (perr != ESP_OK) {
        ESP_LOGE(TAG, "mic power GPIO config failed: %s", esp_err_to_name(perr));
        return perr;
    }
    mic_power(false);
#endif

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0,
                                                            I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num = 8;
    chan_cfg.dma_frame_num = 256;      /* 8 x 16 ms = 128 ms of slack */
    chan_cfg.auto_clear = true;

    esp_err_t err = i2s_new_channel(&chan_cfg, NULL, &s_in.rx);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2s_new_channel(RX) failed: %s", esp_err_to_name(err));
        return err;
    }

    i2s_std_slot_config_t slot_cfg =
        I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT,
                                            I2S_SLOT_MODE_MONO);
    slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;
    slot_cfg.ws_width = I2S_SLOT_BIT_WIDTH_16BIT;
    slot_cfg.ws_pol = false;
    slot_cfg.bit_shift = true;

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_SAMPLE_RATE),
        .slot_cfg = slot_cfg,
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = BOARD_MIC_BCLK_GPIO,
            .ws = BOARD_MIC_WS_GPIO,
            .dout = I2S_GPIO_UNUSED,
            .din = BOARD_MIC_SD_GPIO,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };

    err = i2s_channel_init_std_mode(s_in.rx, &std_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2s_channel_init_std_mode(RX) failed: %s",
                 esp_err_to_name(err));
        i2s_del_channel(s_in.rx);
        s_in.rx = NULL;
        return err;
    }

    s_inited = true;
    ESP_LOGI(TAG, "I2S0 RX ready: %d Hz, 32-bit left slot, BCLK=%d WS=%d SD=%d",
             AUDIO_SAMPLE_RATE, BOARD_MIC_BCLK_GPIO, BOARD_MIC_WS_GPIO,
             BOARD_MIC_SD_GPIO);
    return ESP_OK;
}

esp_err_t audio_in_start(uint32_t ms)
{
    if (!s_inited) {
        return ESP_ERR_INVALID_STATE;
    }
    if (ms < AUDIO_RECORD_MS_MIN) {
        ms = AUDIO_RECORD_MS_MIN;
    } else if (ms > AUDIO_RECORD_MS_MAX) {
        ms = AUDIO_RECORD_MS_MAX;
    }

    xSemaphoreTake(s_in.lock, portMAX_DELAY);
    if (s_in.active) {
        xSemaphoreGive(s_in.lock);
        return ESP_ERR_INVALID_STATE;
    }
    if (audio_out_is_playing()) {
        xSemaphoreGive(s_in.lock);
        return ESP_ERR_INVALID_STATE;
    }

    size_t cap = (size_t)((uint64_t)ms * AUDIO_SAMPLE_RATE / 1000) +
                 SETTLE_SAMPLES + 1600;
    int16_t *pcm = heap_caps_malloc(cap * sizeof(int16_t),
                                    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (pcm == NULL) {
        pcm = malloc(cap * sizeof(int16_t));
    }
    if (pcm == NULL) {
        xSemaphoreGive(s_in.lock);
        return ESP_ERR_NO_MEM;
    }

    s_in.pcm = pcm;
    s_in.cap = cap;
    s_in.count = 0;
    s_in.requested_ms = ms;
    s_in.active = true;
    s_in.done_flag = false;
    s_in.stop_req = false;
    s_in.level_permille = 0;
    s_in.elapsed_ms = 0;
    s_in.start_us = esp_timer_get_time();
    s_in.first_us = 0;
    memset(&s_in.stats, 0, sizeof(s_in.stats));

    mic_power(true);
    vTaskDelay(pdMS_TO_TICKS(10));     /* MS4030 power-on / settle */

    esp_err_t err = i2s_channel_enable(s_in.rx);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2s_channel_enable(RX) failed: %s", esp_err_to_name(err));
        mic_power(false);
        heap_caps_free(pcm);
        s_in.pcm = NULL;
        s_in.active = false;
        xSemaphoreGive(s_in.lock);
        return err;
    }

    BaseType_t ok = xTaskCreatePinnedToCore(audio_in_task, "audio_rec",
                                            REC_TASK_STACK, NULL, 6, NULL, 1);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "xTaskCreate(audio_rec) failed");
        i2s_channel_disable(s_in.rx);
        mic_power(false);
        heap_caps_free(pcm);
        s_in.pcm = NULL;
        s_in.active = false;
        xSemaphoreGive(s_in.lock);
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "record start: %" PRIu32 " ms, buffer=%u KB PSRAM",
             ms, (unsigned)((cap * sizeof(int16_t)) / 1024));
    xSemaphoreGive(s_in.lock);
    return ESP_OK;
}

bool audio_in_is_active(void)
{
    return s_in.active && !s_in.done_flag;
}

bool audio_in_is_done(void)
{
    return s_in.active && s_in.done_flag;
}

uint32_t audio_in_elapsed_ms(void)
{
    return s_in.elapsed_ms;
}

int audio_in_level_permille(void)
{
    int v = s_in.level_permille;
    return v < 0 ? 0 : (v > 1000 ? 1000 : v);
}

esp_err_t audio_in_stop(int16_t **pcm, size_t *frames,
                        audio_capture_stats_t *stats)
{
    if (pcm == NULL || frames == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *pcm = NULL;
    *frames = 0;

    if (!s_inited) {
        return ESP_ERR_INVALID_STATE;
    }
    xSemaphoreTake(s_in.lock, portMAX_DELAY);
    if (!s_in.active) {
        xSemaphoreGive(s_in.lock);
        return ESP_ERR_INVALID_STATE;
    }

    if (!s_in.done_flag) {
        s_in.stop_req = true;
        if (xSemaphoreTake(s_in.done, pdMS_TO_TICKS(3000)) != pdTRUE) {
            ESP_LOGE(TAG, "capture task did not stop in 3 s");
            xSemaphoreGive(s_in.lock);
            return ESP_ERR_TIMEOUT;
        }
    } else {
        /* The task already gave the semaphore; drain it so the next recording
         * does not see a stale completion token. */
        (void)xSemaphoreTake(s_in.done, 0);
    }

    *pcm = s_in.pcm;
    *frames = s_in.count;
    if (stats != NULL) {
        *stats = s_in.stats;
    }
    s_in.pcm = NULL;
    s_in.active = false;
    s_in.done_flag = false;
    s_in.count = 0;

    xSemaphoreGive(s_in.lock);
    return ESP_OK;
}

void audio_in_cancel(void)
{
    int16_t *pcm = NULL;
    size_t frames = 0;
    if (audio_in_stop(&pcm, &frames, NULL) == ESP_OK) {
        audio_in_free(pcm);
    }
}

void audio_in_free(int16_t *pcm)
{
    if (pcm != NULL) {
        heap_caps_free(pcm);
    }
}
