/*
 * audio_common.h — shared audio constants for the M2 audio chain.
 *
 * 所有模块共用同一套采样率/声道/帧参数；PC 端 log-Mel 也按
 * 16 kHz、25 ms 窗、10 ms hop 对齐，改这里必须同步改训练管线。
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

/* 2.0 s at 16 kHz = 32000 samples = 64000 bytes.  The plan allows 2.2 s.
 * 录音缓冲实际会多留 100 ms settle + 100 ms 余量，见 audio_in_start()。 */
#define AUDIO_RECORD_MS_DEFAULT 2000
#define AUDIO_RECORD_MS_MIN     200
#define AUDIO_RECORD_MS_MAX     5000

/* Keep the same frame/hop as the training pipeline (25 ms / 10 ms).
 * 400 点窗 @16 kHz = 25 ms；160 点 hop = 10 ms。 */
#define AUDIO_FRAME_SAMPLES     400
#define AUDIO_HOP_SAMPLES       160

#ifdef __cplusplus
}
#endif
#endif /* AUDIO_COMMON_H */
