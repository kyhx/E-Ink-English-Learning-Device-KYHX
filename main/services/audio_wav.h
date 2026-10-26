/*
 * audio_wav.h — mono 16 kHz PCM16 WAV read/write helpers.
 *
 * M2 stores one 2 s WAV per recording under /storage/rec.  The file name is
 * rec_YYYYmmdd_HHMMSS_mmm.wav.  There is no RTC on the board yet, so the
 * clock is seeded from the firmware build time; M7 replaces it with
 * SNTP / browser time.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef AUDIO_WAV_H
#define AUDIO_WAV_H

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t sample_rate;
    uint16_t channels;
    uint16_t bits_per_sample;
    uint32_t data_offset;
    uint32_t data_bytes;
    uint32_t frames;
    uint32_t duration_ms;
} wav_info_t;

/* Create /storage/rec if needed. */
esp_err_t audio_wav_init(void);

/* Write a mono PCM16 WAV.  `frames` is the number of int16 samples. */
esp_err_t audio_wav_write(const char *path, const int16_t *pcm, size_t frames);

/* Parse the RIFF/fmt/data chunks without loading the payload. */
esp_err_t audio_wav_info(const char *path, wav_info_t *info);

/* Read a PCM16 WAV into a PSRAM buffer, down-mixing stereo to mono.
 * Only 16 kHz, 16-bit, mono/stereo files are accepted in M2. */
esp_err_t audio_wav_read(const char *path, int16_t **pcm, size_t *frames);

/* New unique path under /storage/rec. */
esp_err_t audio_wav_make_rec_path(char *path, size_t path_len);

/* Most recent rec_*.wav under /storage/rec. */
esp_err_t audio_wav_latest(char *path, size_t path_len);

/* Seed the clock from __DATE__/__TIME__ if it is still at the 1970 epoch. */
void audio_wav_seed_clock(void);

/* Human-readable "1.8 s" helper; returns buf. */
const char *audio_wav_duration_str(uint32_t frames, char *buf, size_t buf_len);

#ifdef __cplusplus
}
#endif
#endif /* AUDIO_WAV_H */
