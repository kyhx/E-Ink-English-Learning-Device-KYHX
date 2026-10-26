/*
 * audio.c — initialize and query the M2 audio chain.
 *
 * SPDX-License-Identifier: MIT
 */
#include "audio.h"

#include "esp_log.h"
#include "sdkconfig.h"

#include "audio_in.h"
#include "audio_out.h"
#include "audio_wav.h"

static const char *TAG = "audio";
static bool s_inited;

esp_err_t audio_init(void)
{
    if (s_inited) {
        return ESP_OK;
    }

    esp_err_t err = audio_wav_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "recording dir init failed: %s", esp_err_to_name(err));
        /* Keep going: playback of an existing file may still work. */
    }

    err = audio_in_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "audio_in_init failed: %s", esp_err_to_name(err));
        return err;
    }
    err = audio_out_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "audio_out_init failed: %s", esp_err_to_name(err));
        return err;
    }

    s_inited = true;
    ESP_LOGI(TAG, "M2 audio ready: 16 kHz mono, rec=%d ms",
             CONFIG_APP_AUDIO_RECORD_MS);
    return ESP_OK;
}

bool audio_busy(void)
{
    return audio_in_is_active() || audio_out_is_playing();
}
