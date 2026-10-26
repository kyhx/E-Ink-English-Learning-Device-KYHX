/*
 * audio_out.h — MAX98357A playback on I2S1.
 *
 * 16 kHz / 16-bit stereo (mono sample duplicated to L/R).  AMP_SD (GPIO47)
 * is active high: raise it 10 ms before playback, lower it 50 ms after the
 * last sample.  The speaker outputs are differential and must never be tied
 * to GND.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef AUDIO_OUT_H
#define AUDIO_OUT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t audio_out_init(void);

bool audio_out_is_playing(void);

/* Play a mono 16 kHz PCM16 buffer.  Blocks until done. */
esp_err_t audio_out_play_pcm(const int16_t *pcm, size_t frames);

/* Read a WAV (16 kHz / 16-bit / mono or stereo) and play it. */
esp_err_t audio_out_play_wav(const char *path);

/* Play a sine test tone, e.g. 1000 Hz for 1 s. */
esp_err_t audio_out_play_tone(uint32_t freq_hz, uint32_t ms, uint16_t amplitude);

/* Digital gain applied before I2S TX (100 = unity, 200 = +6 dB).
 * Values are clamped by a hard limiter to avoid int16 overflow. */
esp_err_t audio_out_set_gain_x100(uint16_t gain_x100);
uint16_t audio_out_get_gain_x100(void);

#ifdef __cplusplus
}
#endif
#endif /* AUDIO_OUT_H */
