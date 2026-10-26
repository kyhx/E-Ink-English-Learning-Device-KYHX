/*
 * audio_analysis.c — Goertzel tone scan + basic level statistics.
 *
 * SPDX-License-Identifier: MIT
 */
#include "audio_analysis.h"

#include <math.h>
#include <string.h>

#include "audio_common.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static float goertzel_power(const int16_t *pcm, size_t n, float freq,
                            float *weighted_energy)
{
    float w = 2.0f * (float)M_PI * freq / (float)AUDIO_SAMPLE_RATE;
    float coeff = 2.0f * cosf(w);
    float s0 = 0.0f, s1 = 0.0f, s2 = 0.0f;
    double energy = 0.0;

    for (size_t i = 0; i < n; i++) {
        float x = (float)pcm[i];
        s0 = x + coeff * s1 - s2;
        s2 = s1;
        s1 = s0;
        if (weighted_energy != NULL) {
            energy += (double)x * (double)x;
        }
    }
    if (weighted_energy != NULL) {
        *weighted_energy = (float)energy;
    }
    return s1 * s1 + s2 * s2 - coeff * s1 * s2;
}

void audio_measure(const int16_t *pcm, size_t frames, audio_measure_t *out)
{
    if (out == NULL) {
        return;
    }
    memset(out, 0, sizeof(*out));
    if (pcm == NULL || frames == 0) {
        return;
    }

    double sum = 0.0;
    int32_t peak = 0;
    uint32_t clipped = 0;
    for (size_t i = 0; i < frames; i++) {
        int32_t v = pcm[i];
        sum += v;
        int32_t a = v < 0 ? -v : v;
        if (a > peak) {
            peak = a;
        }
        if (a >= 32700) {
            clipped++;
        }
    }
    out->dc = (int16_t)(sum / (double)frames);
    out->peak = (int16_t)(peak > 32767 ? 32767 : peak);
    out->clipped = clipped;
    out->clip_ratio = (float)clipped / (float)frames;

    double energy = 0.0;
    for (size_t i = 0; i < frames; i++) {
        double v = (double)pcm[i] - out->dc;
        energy += v * v;
    }
    double rms = sqrt(energy / (double)frames);
    out->rms_dbfs = (float)(20.0 * log10(rms / 32768.0 + 1e-12));

    /* Search 700..1300 Hz in 5 Hz steps; a phone 1 kHz test tone is never
     * exactly 1000.0 Hz after the speaker/room. */
    float best_power = 0.0f;
    float best_freq = 0.0f;
    for (float f = 700.0f; f <= 1300.0f; f += 5.0f) {
        float p = goertzel_power(pcm, frames, f, NULL);
        if (p > best_power) {
            best_power = p;
            best_freq = f;
        }
    }

    if (best_power <= 0.0f) {
        return;
    }

    /* Rectangular window: |X| at the bin is A*N/2. */
    double amp = 2.0 * sqrt((double)best_power) / (double)frames;
    double tone_ms = amp * amp / 2.0;
    double total_ms = energy / (double)frames;
    double noise_ms = total_ms - tone_ms;
    if (noise_ms < 1e-9) {
        noise_ms = 1e-9;
    }

    out->tone_hz = best_freq;
    out->tone_dbfs = (float)(10.0 * log10(tone_ms / (32768.0 * 32768.0) + 1e-12));
    out->tone_snr_db = (float)(10.0 * log10(tone_ms / noise_ms + 1e-12));

    /* A noise-only recording often has a "best" bin at -70 dBFS.  Do not
     * report that as a tone. */
    if (out->tone_dbfs < -65.0f || out->tone_snr_db < 3.0f) {
        out->tone_hz = 0.0f;
        out->tone_dbfs = 0.0f;
        out->tone_snr_db = 0.0f;
    }
}
