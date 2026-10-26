/*
 * audio_vad.h — short-time energy + zero-crossing VAD and endpoint trim.
 *
 * The VAD only decides where speech starts/ends; it does not modify the
 * PCM.  The caller trims with speech_start/speech_end (sample indices).
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef AUDIO_VAD_H
#define AUDIO_VAD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool     has_speech;
    uint32_t speech_start;    /* sample index, inclusive */
    uint32_t speech_end;      /* sample index, exclusive */
    uint32_t speech_ms;
    float    noise_dbfs;
    float    speech_dbfs;
    float    snr_db;
    float    zcr;             /* mean ZCR over speech frames */
} audio_vad_result_t;

/* Returns ESP_OK even when has_speech is false. */
esp_err_t audio_vad_analyze(const int16_t *pcm, size_t frames,
                            audio_vad_result_t *out);

#ifdef __cplusplus
}
#endif
#endif /* AUDIO_VAD_H */
