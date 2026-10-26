/*
 * audio_vad.c — frame energy + ZCR endpoint detector.
 *
 * 算法：
 *   1. 20 ms 帧 / 10 ms hop，算每帧 RMS(dBFS) 和过零率；
 *   2. 噪声底取所有帧能量的 20 分位，阈值 = max(噪声+8 dB, -55 dBFS)；
 *   3. 连续两帧超过阈值才算语音起点/终点，避免单点毛刺；
 *   4. 首尾各留 100 ms，有效语音 <300 ms 判无语音；
 *   5. 平均 ZCR <0.008 视为低频哼声/直流建立，不当作单词。
 *
 * SPDX-License-Identifier: MIT
 */
#include "audio_vad.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "audio_common.h"

#define VAD_FRAME   320
#define VAD_HOP     160
#define PAD_SAMPLES 1600          /* 100 ms */
#define MIN_SPEECH_MS 300

static int cmp_float(const void *a, const void *b)
{
    float fa = *(const float *)a;
    float fb = *(const float *)b;
    return (fa > fb) - (fa < fb);
}

/* 一帧的 dBFS + ZCR；dBFS 以 int16 满量程 32768 为 0 dB。 */
static float frame_db(const int16_t *x, size_t n, float *zcr_out)
{
    if (n == 0) {
        return -120.0f;
    }
    double sum = 0.0;
    int crossings = 0;
    int16_t prev = x[0];
    for (size_t i = 0; i < n; i++) {
        int32_t v = x[i];
        sum += (double)v * (double)v;
        if (i > 0 && ((prev < 0 && v >= 0) || (prev >= 0 && v < 0))) {
            crossings++;
        }
        prev = x[i];
    }
    if (zcr_out != NULL) {
        *zcr_out = (float)crossings / (float)(n > 1 ? n - 1 : 1);
    }
    double mean = sum / (double)n;
    double ref = 32768.0 * 32768.0;
    return (float)(10.0 * log10(mean / ref + 1e-12));
}

esp_err_t audio_vad_analyze(const int16_t *pcm, size_t frames,
                            audio_vad_result_t *out)
{
    if (pcm == NULL || out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));

    if (frames < VAD_FRAME) {
        return ESP_ERR_INVALID_SIZE;
    }

    size_t nframes = (frames - VAD_FRAME) / VAD_HOP + 1;
    float *db = malloc(nframes * sizeof(float));
    float *zcr = malloc(nframes * sizeof(float));
    if (db == NULL || zcr == NULL) {
        free(db);
        free(zcr);
        return ESP_ERR_NO_MEM;
    }

    for (size_t i = 0; i < nframes; i++) {
        db[i] = frame_db(pcm + i * VAD_HOP, VAD_FRAME, &zcr[i]);
    }

    float *sorted = malloc(nframes * sizeof(float));
    if (sorted == NULL) {
        free(db);
        free(zcr);
        return ESP_ERR_NO_MEM;
    }
    memcpy(sorted, db, nframes * sizeof(float));
    qsort(sorted, nframes, sizeof(float), cmp_float);
    /* 20 分位而不是最小值：避免把偶发极安静帧当成噪声底。 */
    size_t noise_idx = nframes / 5;                 /* 20th percentile */
    if (noise_idx >= nframes) {
        noise_idx = nframes - 1;
    }
    out->noise_dbfs = sorted[noise_idx];
    free(sorted);

    float threshold = out->noise_dbfs + 8.0f;
    if (threshold < -55.0f) {
        /* 绝对下限：极安静房间里不要把微弱电噪声放大成语音。 */
        threshold = -55.0f;
    }

    bool *speech = calloc(nframes, sizeof(bool));
    if (speech == NULL) {
        free(db);
        free(zcr);
        return ESP_ERR_NO_MEM;
    }
    for (size_t i = 0; i < nframes; i++) {
        /* A pure tone has ZCR~0; fricatives can be high, so keep the upper
         * bound loose and let the energy gate do the main work. */
        speech[i] = (db[i] > threshold) && (zcr[i] < 0.60f);
    }

    size_t first = nframes;
    size_t last = 0;
    /* 起点/终点都要求“连续两帧”为语音，单帧 click 不算。 */
    for (size_t i = 0; i + 1 < nframes; i++) {
        if (speech[i] && speech[i + 1]) {
            first = i;
            break;
        }
    }
    for (size_t i = nframes; i > 1; i--) {
        if (speech[i - 1] && speech[i - 2]) {
            last = i - 1;
            break;
        }
    }

    if (first >= nframes || last < first) {
        /* No two consecutive speech frames: treat as silence. */
        out->speech_start = 0;
        out->speech_end = 0;
        out->speech_ms = 0;
        out->has_speech = false;
        free(speech);
        free(db);
        free(zcr);
        return ESP_OK;
    }

    /* 端点各留 100 ms，避免把首尾辅音切掉；end 不能超过实际帧数。 */
    uint32_t start = (uint32_t)(first * VAD_HOP);
    uint32_t end = (uint32_t)(last * VAD_HOP + VAD_FRAME);
    if (start > PAD_SAMPLES) {
        start -= PAD_SAMPLES;
    } else {
        start = 0;
    }
    end += PAD_SAMPLES;
    if (end > frames) {
        end = (uint32_t)frames;
    }

    double speech_sum = 0.0;
    double zcr_sum = 0.0;
    size_t speech_frames = 0;
    for (size_t i = first; i <= last; i++) {
        if (speech[i]) {
            speech_sum += db[i];
            zcr_sum += zcr[i];
            speech_frames++;
        }
    }
    out->speech_dbfs = speech_frames ? (float)(speech_sum / speech_frames)
                                     : out->noise_dbfs;
    out->zcr = speech_frames ? (float)(zcr_sum / speech_frames) : 0.0f;
    out->speech_start = start;
    out->speech_end = end;
    out->speech_ms = (end - start) * 1000 / AUDIO_SAMPLE_RATE;
    out->snr_db = out->speech_dbfs - out->noise_dbfs;
    /* Low-frequency hum/DC settling has a very low ZCR and should not be
     * accepted as a word.  Voiced speech is typically 0.02–0.15. */
    out->has_speech = (out->speech_ms >= MIN_SPEECH_MS) &&
                      (out->zcr >= 0.008f);

    free(speech);
    free(db);
    free(zcr);
    return ESP_OK;
}
