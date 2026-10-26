/*
 * audio_out.c — I2S1 TX + MAX98357A enable sequencing.
 *
 * SPDX-License-Identifier: MIT
 */
#include "audio_out.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "sdkconfig.h"

#include "audio_common.h"
#include "audio_in.h"
#include "audio_wav.h"
#include "board_pins.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static const char *TAG = "audio_out";

#define OUT_CHUNK_FRAMES 256
#define OUT_TIMEOUT_MS   100

static i2s_chan_handle_t s_tx;
static volatile bool s_playing;
static bool s_inited;

static void amp_enable(bool on)
{
#if CONFIG_APP_AUDIO_AMP_SD_ACTIVE_HIGH
    gpio_set_level(BOARD_AMP_SD_GPIO, on ? 1 : 0);
#else
    gpio_set_level(BOARD_AMP_SD_GPIO, on ? 0 : 1);
#endif
}

static esp_err_t write_silence(uint32_t ms)
{
    int16_t zeros[OUT_CHUNK_FRAMES * 2] = {0};
    size_t total = (size_t)((uint64_t)AUDIO_SAMPLE_RATE * ms / 1000);
    while (total > 0) {
        size_t n = total > OUT_CHUNK_FRAMES ? OUT_CHUNK_FRAMES : total;
        size_t written = 0;
        esp_err_t err = i2s_channel_write(s_tx, zeros, n * 2 * sizeof(int16_t),
                                         &written, pdMS_TO_TICKS(OUT_TIMEOUT_MS));
        if (err != ESP_OK) {
            return err;
        }
        total -= n;
    }
    return ESP_OK;
}

static esp_err_t write_pcm(const int16_t *pcm, size_t frames)
{
    int16_t stereo[OUT_CHUNK_FRAMES * 2];
    size_t done = 0;
    while (done < frames) {
        size_t n = frames - done;
        if (n > OUT_CHUNK_FRAMES) {
            n = OUT_CHUNK_FRAMES;
        }
        for (size_t i = 0; i < n; i++) {
            stereo[i * 2] = pcm[done + i];
            stereo[i * 2 + 1] = pcm[done + i];
        }
        size_t written = 0;
        esp_err_t err = i2s_channel_write(s_tx, stereo, n * 2 * sizeof(int16_t),
                                         &written, pdMS_TO_TICKS(OUT_TIMEOUT_MS));
        if (err != ESP_OK) {
            return err;
        }
        if (written != n * 2 * sizeof(int16_t)) {
            return ESP_FAIL;
        }
        done += n;
    }
    return ESP_OK;
}

esp_err_t audio_out_init(void)
{
    if (s_inited) {
        return ESP_OK;
    }

    gpio_config_t amp = {
        .pin_bit_mask = 1ULL << BOARD_AMP_SD_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&amp);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "AMP SD GPIO config failed: %s", esp_err_to_name(err));
        return err;
    }
    amp_enable(false);

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1,
                                                            I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num = 8;
    chan_cfg.dma_frame_num = 256;
    chan_cfg.auto_clear = true;

    err = i2s_new_channel(&chan_cfg, &s_tx, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2s_new_channel(TX) failed: %s", esp_err_to_name(err));
        return err;
    }

    i2s_std_slot_config_t slot_cfg =
        I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                            I2S_SLOT_MODE_STEREO);
    slot_cfg.ws_width = I2S_SLOT_BIT_WIDTH_16BIT;
    slot_cfg.ws_pol = false;
    slot_cfg.bit_shift = true;

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_SAMPLE_RATE),
        .slot_cfg = slot_cfg,
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = BOARD_AMP_BCLK_GPIO,
            .ws = BOARD_AMP_LRC_GPIO,
            .dout = BOARD_AMP_DIN_GPIO,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };

    err = i2s_channel_init_std_mode(s_tx, &std_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2s_channel_init_std_mode(TX) failed: %s",
                 esp_err_to_name(err));
        i2s_del_channel(s_tx);
        s_tx = NULL;
        return err;
    }

    s_inited = true;
    ESP_LOGI(TAG, "I2S1 TX ready: %d Hz, 16-bit stereo, BCLK=%d LRC=%d DIN=%d SD=%d",
             AUDIO_SAMPLE_RATE, BOARD_AMP_BCLK_GPIO, BOARD_AMP_LRC_GPIO,
             BOARD_AMP_DIN_GPIO, BOARD_AMP_SD_GPIO);
    return ESP_OK;
}

bool audio_out_is_playing(void)
{
    return s_playing;
}

esp_err_t audio_out_play_pcm(const int16_t *pcm, size_t frames)
{
    if (!s_inited) {
        return ESP_ERR_INVALID_STATE;
    }
    if (pcm == NULL || frames == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (audio_in_is_active()) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_playing) {
        return ESP_ERR_INVALID_STATE;
    }

    s_playing = true;
    esp_err_t err = ESP_OK;

    amp_enable(true);
    vTaskDelay(pdMS_TO_TICKS(10));       /* MAX98357A turn-on / de-pop */
    err = i2s_channel_enable(s_tx);
    if (err != ESP_OK) {
        goto out;
    }

    /* A short silence before the first sample avoids a click. */
    err = write_silence(20);
    if (err == ESP_OK) {
        err = write_pcm(pcm, frames);
    }
    if (err == ESP_OK) {
        err = write_silence(20);
    }
    i2s_channel_disable(s_tx);

out:
    vTaskDelay(pdMS_TO_TICKS(50));       /* let the amp settle before mute */
    amp_enable(false);
    s_playing = false;
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "playback failed: %s", esp_err_to_name(err));
    }
    return err;
}

esp_err_t audio_out_play_wav(const char *path)
{
    if (path == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    int16_t *pcm = NULL;
    size_t frames = 0;
    esp_err_t err = audio_wav_read(path, &pcm, &frames);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "read %s failed: %s", path, esp_err_to_name(err));
        return err;
    }
    err = audio_out_play_pcm(pcm, frames);
    heap_caps_free(pcm);
    return err;
}

static esp_err_t tone_common(uint32_t freq_hz, uint32_t ms, uint16_t amplitude)
{
    if (freq_hz == 0 || ms == 0 || amplitude == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (ms > 5000) {
        ms = 5000;
    }
    size_t frames = (size_t)((uint64_t)AUDIO_SAMPLE_RATE * ms / 1000);
    int16_t *pcm = heap_caps_malloc(frames * sizeof(int16_t),
                                    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (pcm == NULL) {
        pcm = malloc(frames * sizeof(int16_t));
    }
    if (pcm == NULL) {
        return ESP_ERR_NO_MEM;
    }

    for (size_t i = 0; i < frames; i++) {
        float phase = 2.0f * (float)M_PI * (float)freq_hz * (float)i /
                      (float)AUDIO_SAMPLE_RATE;
        pcm[i] = (int16_t)(sinf(phase) * (float)amplitude);
    }

    esp_err_t err = audio_out_play_pcm(pcm, frames);
    heap_caps_free(pcm);
    return err;
}

esp_err_t audio_out_play_tone(uint32_t freq_hz, uint32_t ms, uint16_t amplitude)
{
    return tone_common(freq_hz, ms, amplitude);
}
