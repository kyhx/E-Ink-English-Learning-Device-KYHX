/*
 * audio_analysis.h — level / clipping / 1 kHz tone measurements.
 *
 * Used by `bench audio rec` and `bench audio selftest` to check the analog
 * chain with a phone tone: peak, RMS, DC, clipping, dominant tone and SNR.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef AUDIO_ANALYSIS_H
#define AUDIO_ANALYSIS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float    rms_dbfs;
    int16_t  peak;
    int16_t  dc;
    uint32_t clipped;       /* |sample| >= 32700 */
    float    clip_ratio;    /* 0..1 */
    float    tone_hz;       /* 700..1300 Hz search, 0 if none */
    float    tone_dbfs;
    float    tone_snr_db;
} audio_measure_t;

void audio_measure(const int16_t *pcm, size_t frames, audio_measure_t *out);

#ifdef __cplusplus
}
#endif
#endif /* AUDIO_ANALYSIS_H */
