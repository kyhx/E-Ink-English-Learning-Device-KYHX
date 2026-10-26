/*
 * audio_in.h — MS4030 / INMP441 capture on I2S0.
 *
 * 16 kHz, 32-bit slot (24-bit mic data left aligned), left channel, mono.
 * Recording runs in its own FreeRTOS task so a 346 ms e-paper refresh cannot
 * overrun the I2S DMA.  The PCM buffer lives in PSRAM (64 KB for 2 s).
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef AUDIO_IN_H
#define AUDIO_IN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t requested_ms;
    uint32_t captured_ms;
    uint32_t start_latency_ms;   /* start call -> first DMA sample */
    uint16_t peak;
    uint16_t rms;
    int16_t  dc;
    uint32_t clipped;
    uint32_t read_errors;
} audio_capture_stats_t;

esp_err_t audio_in_init(void);

/* Start a capture of `ms` (clamped to 200..5000).  Non-blocking. */
esp_err_t audio_in_start(uint32_t ms);

bool audio_in_is_active(void);
bool audio_in_is_done(void);
uint32_t audio_in_elapsed_ms(void);
int audio_in_level_permille(void);

/* Wait for the capture task if needed, then hand ownership of the PSRAM PCM
 * buffer to the caller.  Free it with audio_in_free(). */
esp_err_t audio_in_stop(int16_t **pcm, size_t *frames,
                        audio_capture_stats_t *stats);

/* Stop and discard the current capture. */
void audio_in_cancel(void);

void audio_in_free(int16_t *pcm);

#ifdef __cplusplus
}
#endif
#endif /* AUDIO_IN_H */
