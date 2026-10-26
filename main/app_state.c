/*
 * app_state.c — M2 home screen, recording feedback and playback.
 *
 * The audio capture task runs independently of this loop, so the 346 ms
 * band refresh never causes an I2S overrun.  RECORD captures 2 s, runs VAD,
 * trims 100 ms of padding, writes a WAV and shows the result.  PLAY replays
 * the latest recording.
 *
 * SPDX-License-Identifier: MIT
 */
#include "app_state.h"

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "sdkconfig.h"

#include "audio_common.h"
#include "audio_in.h"
#include "audio_out.h"
#include "audio_vad.h"
#include "audio_wav.h"
#include "buttons.h"
#include "epd_ssd1619a.h"

static const char *TAG = "app";

#define HOME_BAND_Y0 95
#define HOME_BAND_Y1 164
#define UI_UPDATE_MS 500

static uint8_t s_base[EPD_FRAME_BYTES];
static uint8_t s_frame[EPD_FRAME_BYTES];
static char s_last_path[160];
static bool s_has_last;
static bool s_recording;
static uint32_t s_next_ui_ms;

static void draw_centered_scaled(uint8_t *fb, int y, const char *text, int scale)
{
    int w = epd_fb_text_width_scaled(text, scale);
    epd_fb_text_scaled(fb, (EPD_WIDTH - w) / 2, y, text, 0, scale);
}

static void render_home_base(void)
{
    epd_fb_clear(s_base, 1);
    epd_fb_rect(s_base, 0, 0, EPD_WIDTH, EPD_HEIGHT, 0);

    draw_centered_scaled(s_base, 28, "E-INK WORDS", 3);
    draw_centered_scaled(s_base, 178, "M2 / AUDIO", 2);
    draw_centered_scaled(s_base, 258, "REC=2s  PLAY=last", 1);
}

static void show_band_text(const char *text, int scale)
{
    memcpy(s_frame, s_base, EPD_FRAME_BYTES);
    int w = epd_fb_text_width_scaled(text, scale);
    int y = HOME_BAND_Y0 + (HOME_BAND_Y1 - HOME_BAND_Y0 + 1 - 8 * scale) / 2;
    epd_fb_text_scaled(s_frame, (EPD_WIDTH - w) / 2, y, text, 0, scale);
    epd_band_update(s_frame, HOME_BAND_Y0, HOME_BAND_Y1, 5000);
}

static void show_record_screen(uint32_t elapsed_ms, int level_permille)
{
    memcpy(s_frame, s_base, EPD_FRAME_BYTES);

    char line[24];
    snprintf(line, sizeof(line), "REC %u.%u s",
             (unsigned)(elapsed_ms / 1000),
             (unsigned)((elapsed_ms % 1000) / 100));
    int w = epd_fb_text_width_scaled(line, 2);
    epd_fb_text_scaled(s_frame, (EPD_WIDTH - w) / 2, 100, line, 0, 2);

    /* Level bar: 240 px track, 236 px usable, 12 px high. */
    epd_fb_rect(s_frame, 80, 138, 240, 16, 0);
    int fill = level_permille * 236 / 1000;
    if (fill > 0) {
        epd_fb_fill_rect(s_frame, 82, 140, fill, 12, 0);
    }

    epd_band_update(s_frame, HOME_BAND_Y0, HOME_BAND_Y1, 5000);
}

static void show_result(const char *text, int scale)
{
    show_band_text(text, scale);
}

static void refresh_last_path(void)
{
    s_has_last = audio_wav_latest(s_last_path, sizeof(s_last_path)) == ESP_OK;
    if (s_has_last) {
        ESP_LOGI(TAG, "latest recording: %s", s_last_path);
    }
}

static void save_recording(int16_t *pcm, size_t frames,
                           const audio_capture_stats_t *stats)
{
    audio_vad_result_t vad;
    esp_err_t err = audio_vad_analyze(pcm, frames, &vad);
    if (err != ESP_OK) {
        show_result("VAD ERR", 3);
        return;
    }

    if (!vad.has_speech) {
        ESP_LOGW(TAG, "no speech: %.1f dBFS noise, speech %.1f dBFS, %.1f dB SNR",
                 vad.noise_dbfs, vad.speech_dbfs, vad.snr_db);
        show_result("NO SPEECH", 3);
        return;
    }

    size_t start = vad.speech_start;
    size_t end = vad.speech_end;
    if (end <= start || end > frames) {
        show_result("VAD ERR", 3);
        return;
    }
    size_t n = end - start;

    char path[160];
    err = audio_wav_make_rec_path(path, sizeof(path));
    if (err == ESP_OK) {
        err = audio_wav_write(path, pcm + start, n);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "save failed: %s", esp_err_to_name(err));
        show_result("SAVE ERR", 3);
        return;
    }

    snprintf(s_last_path, sizeof(s_last_path), "%s", path);
    s_has_last = true;

    char dur[16];
    char line[32];
    audio_wav_duration_str((uint32_t)n, dur, sizeof(dur));
    if (stats != NULL && stats->clipped > (frames / 200)) {
        snprintf(line, sizeof(line), "CLIP %s", dur);
    } else {
        snprintf(line, sizeof(line), "SAVED %s", dur);
    }
    show_result(line, 3);

    ESP_LOGI(TAG, "saved %s: %u frames, vad=%u..%u, snr=%.1f dB, clip=%u",
             path, (unsigned)n, (unsigned)start, (unsigned)end,
             vad.snr_db, stats ? (unsigned)stats->clipped : 0);
}

static void start_recording(void)
{
    if (s_recording || audio_out_is_playing()) {
        return;
    }
    esp_err_t err = audio_in_start(CONFIG_APP_AUDIO_RECORD_MS);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "record start failed: %s", esp_err_to_name(err));
        show_result("AUDIO ERR", 3);
        return;
    }
    s_recording = true;
    s_next_ui_ms = 0;
    ESP_LOGI(TAG, "recording %d ms", CONFIG_APP_AUDIO_RECORD_MS);
}

static void finish_recording(void)
{
    int16_t *pcm = NULL;
    size_t frames = 0;
    audio_capture_stats_t stats;
    esp_err_t err = audio_in_stop(&pcm, &frames, &stats);
    s_recording = false;
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "record stop failed: %s", esp_err_to_name(err));
        show_result("AUDIO ERR", 3);
        return;
    }
    if (frames == 0) {
        show_result("NO SAMPLES", 3);
        audio_in_free(pcm);
        return;
    }

    ESP_LOGI(TAG, "captured %u frames, start=%u ms, peak=%u rms=%u clip=%u",
             (unsigned)frames, stats.start_latency_ms, stats.peak, stats.rms,
             (unsigned)stats.clipped);
    save_recording(pcm, frames, &stats);
    audio_in_free(pcm);
}

static void play_latest(void)
{
    if (s_recording || audio_out_is_playing()) {
        return;
    }
    if (!s_has_last) {
        refresh_last_path();
    }
    if (!s_has_last) {
        show_result("NO REC", 3);
        return;
    }

    ESP_LOGI(TAG, "play %s", s_last_path);
    show_result("PLAYING", 3);
    esp_err_t err = audio_out_play_wav(s_last_path);
    if (err == ESP_OK) {
        show_result("DONE", 3);
    } else {
        show_result("PLAY ERR", 3);
    }
}

static void handle_event(button_event_t ev)
{
    switch (ev) {
    case BUTTON_EVENT_RECORD_SHORT:
        start_recording();
        break;
    case BUTTON_EVENT_PLAY_SHORT:
        play_latest();
        break;
    case BUTTON_EVENT_PREV_SHORT:
        show_result("PREV", 3);
        break;
    case BUTTON_EVENT_NEXT_SHORT:
        show_result("NEXT", 3);
        break;
    case BUTTON_EVENT_SYNC_SHORT:
        show_result("SYNC", 3);
        break;
    default:
        break;
    }
}

void app_state_run(void)
{
    render_home_base();
    memcpy(s_frame, s_base, EPD_FRAME_BYTES);
    epd_partial_show_full(s_frame, 5000);
    show_result("READY", 4);
    refresh_last_path();

    ESP_LOGI(TAG, "M2 app ready: RECORD=2s, PLAY=latest, bench audio ...");

    while (true) {
        uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
        button_event_t ev = buttons_poll(now_ms);

        if (ev != BUTTON_EVENT_NONE) {
            handle_event(ev);
        }

        if (s_recording) {
            if (audio_in_is_done()) {
                finish_recording();
            } else if ((int32_t)(now_ms - s_next_ui_ms) >= 0) {
                show_record_screen(audio_in_elapsed_ms(),
                                   audio_in_level_permille());
                s_next_ui_ms = now_ms + UI_UPDATE_MS;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
