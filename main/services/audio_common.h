/*
 * audio_common.h — shared audio constants for the M2 audio chain.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef AUDIO_COMMON_H
#define AUDIO_COMMON_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AUDIO_SAMPLE_RATE       16000
#define AUDIO_BITS_PER_SAMPLE   16
#define AUDIO_CHANNELS          1

#define AUDIO_REC_DIR           "/storage/rec"

/* 2.0 s at 16 kHz = 32000 samples = 64000 bytes.  The plan allows 2.2 s. */
#define AUDIO_RECORD_MS_DEFAULT 2000
#define AUDIO_RECORD_MS_MIN     200
#define AUDIO_RECORD_MS_MAX     5000

/* Keep the same frame/hop as the training pipeline (25 ms / 10 ms). */
#define AUDIO_FRAME_SAMPLES     400
#define AUDIO_HOP_SAMPLES       160

#ifdef __cplusplus
}
#endif
#endif /* AUDIO_COMMON_H */
