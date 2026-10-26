/*
 * audio.h — M2 audio chain facade.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef AUDIO_H
#define AUDIO_H

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Init LittleFS recording dir, I2S0 RX and I2S1 TX. */
esp_err_t audio_init(void);

/* True while recording or playing; bench/app use it to avoid collisions. */
bool audio_busy(void);

#ifdef __cplusplus
}
#endif
#endif /* AUDIO_H */
