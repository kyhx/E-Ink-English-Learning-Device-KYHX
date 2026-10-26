/*
 * audio_in.c — I2S0 RX capture task for the MS4030 (INMP441 compatible).
 *
 * 数据路径：
 *   MS4030 --I2S0--> 32-bit DMA --> 取高 16 bit --> 数字增益
 *          --> PSRAM PCM 缓冲 --> audio_in_stop() 交给调用者
 *
 * 关键设计：
 *   1. 采集放在独立任务里，优先级高于 UI；这样 346 ms 的墨水屏带状刷新
 *      不会让 I2S DMA 溢出。
 *   2. 上电后先丢弃 100 ms。实测 MS4030 上电/直流建立会在前 100 ms 产生
 *      约 29k 的瞬态，不丢弃会被 VAD 误判成语音。
 *   3. 只申请一块 PSRAM，录完把指针交给调用者；不要在任务里 free，否则
 *      UI 还没保存就已经释放。
 *
 * SPDX-License-Identifier: MIT
 */
#include "audio_in.h"

#include <inttypes.h>
#include <math.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "sdkconfig.h"

#include "audio_common.h"
#include "audio_out.h"
#include "board_pins.h"

static const char *TAG = "audio_in";

#define REC_CHUNK_FRAMES 160       /* 10 ms @ 16 kHz；一次读一小块，便于刷新电平 */
#define REC_TASK_STACK   4096      /* 只做整数搬运和统计，4 KB 足够 */
#define SETTLE_SAMPLES   1600      /* 100 ms：MS4030 上电/直流建立，必须丢弃 */

/*
 * 录音上下文。lock 保护 start/stop 的生命周期；done 是任务完成信号；
 * active/done_flag/stop_req/level 是跨任务可见的 volatile 标志。
 */
typedef struct {
    i2s_chan_handle_t rx;
    SemaphoreHandle_t lock;
    SemaphoreHandle_t done;
    int16_t *pcm;                  /* PSRAM 缓冲，所有权在 stop 时转移 */
    size_t cap;                    /* 缓冲总样本数（含 settle 余量） */
    size_t count;                  /* 已写入的有效样本数 */

    volatile bool active;          /* start 已成功，等待 stop */
    volatile bool done_flag;       /* 采集任务已结束 */
    volatile bool stop_req;        /* 请求提前结束 */
    volatile int level_permille;   /* 最近 10 ms 的峰值，0..1000 */
    volatile uint32_t elapsed_ms;  /* 有效样本对应的时长 */

    int64_t start_us;              /* 调用 start 的时刻 */
    int64_t first_us;              /* 第一个有效样本的时刻 */
    uint32_t requested_ms;
    audio_capture_stats_t stats;
} audio_in_ctx_t;

static audio_in_ctx_t s_in;
static bool s_inited;

/* GPIO38 可选：接 MOS/负载开关时控制 MS4030 的 3.3 V；未接则宏为 -1。 */
static void mic_power(bool on)
{
#if CONFIG_APP_AUDIO_MIC_POWER_GPIO >= 0
    gpio_set_level((gpio_num_t)CONFIG_APP_AUDIO_MIC_POWER_GPIO, on ? 1 : 0);
#else
    (void)on;
#endif
}

/* 统计峰值/RMS/DC/削顶；RMS 先减 DC，避免直流偏置把响度算高。 */
static void fill_stats(void)
{
    size_t n = s_in.count;
    if (n == 0) {
        return;
    }

    int64_t sum = 0;
    int32_t peak = 0;
    uint32_t clipped = 0;
    for (size_t i = 0; i < n; i++) {
        int32_t v = s_in.pcm[i];
        sum += v;
        int32_t a = v < 0 ? -v : v;
        if (a > peak) {
            peak = a;
        }
        if (a >= 32700) {
            clipped++;
        }
    }
    int32_t dc = (int32_t)(sum / (int64_t)n);

    double energy = 0.0;
    for (size_t i = 0; i < n; i++) {
        double v = (double)s_in.pcm[i] - dc;
        energy += v * v;
    }

    s_in.stats.requested_ms = s_in.requested_ms;
    s_in.stats.captured_ms = (uint32_t)((uint64_t)n * 1000 / AUDIO_SAMPLE_RATE);
    s_in.stats.start_latency_ms = (uint32_t)((s_in.first_us - s_in.start_us) / 1000);
    s_in.stats.peak = (uint16_t)(peak > 65535 ? 65535 : peak);
    s_in.stats.rms = (uint16_t)sqrt(energy / (double)n);
    s_in.stats.dc = (int16_t)dc;
    s_in.stats.clipped = clipped;
}

/*
 * 采集任务：
 *   - i2s_channel_read 每次拿 10 ms 的 32-bit 原始样本；
 *   - 前 SETTLE_SAMPLES 个样本只计数、不写入；
 *   - 之后右移 16 bit 得到 16-bit PCM，乘数字增益并做饱和；
 *   - 写满 requested_ms 后关 I2S、关麦克风电源、发完成信号。
 */
static void audio_in_task(void *arg)
{
    (void)arg;
    int32_t raw[REC_CHUNK_FRAMES];
    size_t target = (size_t)((uint64_t)s_in.requested_ms * AUDIO_SAMPLE_RATE / 1000);
    if (target > s_in.cap) {
        target = s_in.cap;
    }
    size_t settle = SETTLE_SAMPLES;

    while (!s_in.stop_req && s_in.count < target) {
        size_t bytes = 0;
        esp_err_t err = i2s_channel_read(s_in.rx, raw, sizeof(raw), &bytes,
                                         pdMS_TO_TICKS(200));
        if (err == ESP_ERR_TIMEOUT) {
            continue;
        }
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "i2s read failed: %s", esp_err_to_name(err));
            s_in.stats.read_errors++;
            break;
        }
        if (bytes == 0) {
            /* 没有数据时主动让出 CPU，避免空转饿死 bench/UI 任务。 */
            vTaskDelay(pdMS_TO_TICKS(1));
            continue;
        }
        size_t got = bytes / sizeof(int32_t);
        int peak = 0;
        for (size_t i = 0; i < got && s_in.count < target; i++) {
            if (settle > 0) {
                settle--;
                continue;
            }
            if (s_in.first_us == 0) {
                s_in.first_us = esp_timer_get_time();
            }
            /* INMP441/MS4030：24 位有效数据，MSB 在前，左对齐在 32-bit
             * 槽里。右移 16 位就是取高 16 位；不要按低 16 位读。
             */
            int32_t v = raw[i] >> 16;
            v = (v * CONFIG_APP_AUDIO_MIC_GAIN_X100) / 100;
            if (v > 32767) {
                v = 32767;
            } else if (v < -32768) {
                v = -32768;
            }
            s_in.pcm[s_in.count++] = (int16_t)v;
            int a = v < 0 ? -v : v;
            if (a > peak) {
                peak = a;
            }
        }
        s_in.level_permille = peak * 1000 / 32768;
        s_in.elapsed_ms = (uint32_t)((uint64_t)s_in.count * 1000 /
                                     AUDIO_SAMPLE_RATE);
    }

    i2s_channel_disable(s_in.rx);
    mic_power(false);
    fill_stats();

    s_in.done_flag = true;
    s_in.level_permille = 0;
    xSemaphoreGive(s_in.done);
    vTaskDelete(NULL);
}

esp_err_t audio_in_init(void)
{
    if (s_inited) {
        return ESP_OK;
    }

    s_in.lock = xSemaphoreCreateMutex();
    s_in.done = xSemaphoreCreateBinary();
    if (s_in.lock == NULL || s_in.done == NULL) {
        return ESP_ERR_NO_MEM;
    }

#if CONFIG_APP_AUDIO_MIC_POWER_GPIO >= 0
    gpio_config_t pwr = {
        .pin_bit_mask = 1ULL << CONFIG_APP_AUDIO_MIC_POWER_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t perr = gpio_config(&pwr);
    if (perr != ESP_OK) {
        ESP_LOGE(TAG, "mic power GPIO config failed: %s", esp_err_to_name(perr));
        return perr;
    }
    mic_power(false);
#endif

    /* 8 个 DMA 描述符 × 256 帧 ≈ 128 ms 余量；采集任务按 10 ms 读，
     * 即使 UI 短暂占用 CPU 也不会立刻 overrun。
     */
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0,
                                                            I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num = 8;
    chan_cfg.dma_frame_num = 256;      /* 8 x 16 ms = 128 ms of slack */
    chan_cfg.auto_clear = true;

    esp_err_t err = i2s_new_channel(&chan_cfg, NULL, &s_in.rx);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2s_new_channel(RX) failed: %s", esp_err_to_name(err));
        return err;
    }

    /* Philips I2S、32-bit 槽、单声道左槽；INMP441 的 L/R 接地即左声道。
     * ws_width=16 与 TFLM 的 micro_speech 示例一致，实测可正常收数。
     */
    i2s_std_slot_config_t slot_cfg =
        I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT,
                                            I2S_SLOT_MODE_MONO);
    slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;
    slot_cfg.ws_width = I2S_SLOT_BIT_WIDTH_16BIT;
    slot_cfg.ws_pol = false;
    slot_cfg.bit_shift = true;

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_SAMPLE_RATE),
        .slot_cfg = slot_cfg,
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,   /* INMP441 不需要 MCLK */
            .bclk = BOARD_MIC_BCLK_GPIO,
            .ws = BOARD_MIC_WS_GPIO,
            .dout = I2S_GPIO_UNUSED,   /* 只收不发 */
            .din = BOARD_MIC_SD_GPIO,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };

    err = i2s_channel_init_std_mode(s_in.rx, &std_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2s_channel_init_std_mode(RX) failed: %s",
                 esp_err_to_name(err));
        i2s_del_channel(s_in.rx);
        s_in.rx = NULL;
        return err;
    }

    s_inited = true;
    ESP_LOGI(TAG, "I2S0 RX ready: %d Hz, 32-bit left slot, BCLK=%d WS=%d SD=%d",
             AUDIO_SAMPLE_RATE, BOARD_MIC_BCLK_GPIO, BOARD_MIC_WS_GPIO,
             BOARD_MIC_SD_GPIO);
    return ESP_OK;
}

/*
 * 启动一次采集。这个函数只做“申请缓冲 + 开外设 + 建任务”，立即返回；
 * 调用者之后用 audio_in_is_done()/audio_in_stop() 收割。
 */
esp_err_t audio_in_start(uint32_t ms)
{
    if (!s_inited) {
        return ESP_ERR_INVALID_STATE;
    }
    if (ms < AUDIO_RECORD_MS_MIN) {
        ms = AUDIO_RECORD_MS_MIN;
    } else if (ms > AUDIO_RECORD_MS_MAX) {
        ms = AUDIO_RECORD_MS_MAX;
    }

    xSemaphoreTake(s_in.lock, portMAX_DELAY);
    if (s_in.active) {
        xSemaphoreGive(s_in.lock);
        return ESP_ERR_INVALID_STATE;
    }
    if (audio_out_is_playing()) {
        xSemaphoreGive(s_in.lock);
        return ESP_ERR_INVALID_STATE;
    }

    /* 多留 settle + 100 ms 余量，但只写 requested_ms 的有效样本。 */
    size_t cap = (size_t)((uint64_t)ms * AUDIO_SAMPLE_RATE / 1000) +
                 SETTLE_SAMPLES + 1600;
    int16_t *pcm = heap_caps_malloc(cap * sizeof(int16_t),
                                    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (pcm == NULL) {
        pcm = malloc(cap * sizeof(int16_t));
    }
    if (pcm == NULL) {
        xSemaphoreGive(s_in.lock);
        return ESP_ERR_NO_MEM;
    }

    s_in.pcm = pcm;
    s_in.cap = cap;
    s_in.count = 0;
    s_in.requested_ms = ms;
    s_in.active = true;
    s_in.done_flag = false;
    s_in.stop_req = false;
    s_in.level_permille = 0;
    s_in.elapsed_ms = 0;
    s_in.start_us = esp_timer_get_time();
    s_in.first_us = 0;
    memset(&s_in.stats, 0, sizeof(s_in.stats));

    mic_power(true);
    vTaskDelay(pdMS_TO_TICKS(10));     /* MS4030 供电建立，之后任务里再丢 100 ms */

    esp_err_t err = i2s_channel_enable(s_in.rx);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2s_channel_enable(RX) failed: %s", esp_err_to_name(err));
        mic_power(false);
        heap_caps_free(pcm);
        s_in.pcm = NULL;
        s_in.active = false;
        xSemaphoreGive(s_in.lock);
        return err;
    }

    BaseType_t ok = xTaskCreatePinnedToCore(audio_in_task, "audio_rec",
                                            REC_TASK_STACK, NULL, 6, NULL, 1);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "xTaskCreate(audio_rec) failed");
        i2s_channel_disable(s_in.rx);
        mic_power(false);
        heap_caps_free(pcm);
        s_in.pcm = NULL;
        s_in.active = false;
        xSemaphoreGive(s_in.lock);
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "record start: %" PRIu32 " ms, buffer=%u KB PSRAM",
             ms, (unsigned)((cap * sizeof(int16_t)) / 1024));
    xSemaphoreGive(s_in.lock);
    return ESP_OK;
}

/* active 在 stop 之前一直为 true；done_flag 用于区分“正在录/已录完”。 */
bool audio_in_is_active(void)
{
    return s_in.active && !s_in.done_flag;
}

bool audio_in_is_done(void)
{
    return s_in.active && s_in.done_flag;
}

uint32_t audio_in_elapsed_ms(void)
{
    return s_in.elapsed_ms;
}

int audio_in_level_permille(void)
{
    int v = s_in.level_permille;
    return v < 0 ? 0 : (v > 1000 ? 1000 : v);
}

/*
 * 等待任务结束并移交 PCM 缓冲。正常调用时任务已经 done；如果调用者想
 * 提前停止（例如 UI 取消），设置 stop_req 再等 3 s。
 *
 * 注意：完成后必须把 done 信号量取空，否则下一次录音会立刻看到上一次的
 * 完成信号，误以为任务已经结束。
 */
esp_err_t audio_in_stop(int16_t **pcm, size_t *frames,
                        audio_capture_stats_t *stats)
{
    if (pcm == NULL || frames == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *pcm = NULL;
    *frames = 0;

    if (!s_inited) {
        return ESP_ERR_INVALID_STATE;
    }
    xSemaphoreTake(s_in.lock, portMAX_DELAY);
    if (!s_in.active) {
        xSemaphoreGive(s_in.lock);
        return ESP_ERR_INVALID_STATE;
    }

    if (!s_in.done_flag) {
        s_in.stop_req = true;
        if (xSemaphoreTake(s_in.done, pdMS_TO_TICKS(3000)) != pdTRUE) {
            ESP_LOGE(TAG, "capture task did not stop in 3 s");
            xSemaphoreGive(s_in.lock);
            return ESP_ERR_TIMEOUT;
        }
    } else {
        /* 任务已经给过信号量；这里 drain 掉，避免下一次 start 误判。 */
        (void)xSemaphoreTake(s_in.done, 0);
    }

    *pcm = s_in.pcm;
    *frames = s_in.count;
    if (stats != NULL) {
        *stats = s_in.stats;
    }
    s_in.pcm = NULL;
    s_in.active = false;
    s_in.done_flag = false;
    s_in.count = 0;

    xSemaphoreGive(s_in.lock);
    return ESP_OK;
}

void audio_in_cancel(void)
{
    int16_t *pcm = NULL;
    size_t frames = 0;
    if (audio_in_stop(&pcm, &frames, NULL) == ESP_OK) {
        audio_in_free(pcm);
    }
}

void audio_in_free(int16_t *pcm)
{
    if (pcm != NULL) {
        heap_caps_free(pcm);
    }
}
