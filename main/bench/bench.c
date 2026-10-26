/*
 * bench.c — serial console commands for board bring-up.
 *
 * 运行在 esp_console 自己的 REPL 任务里；M0 还没有显示互斥锁，所以
 * display 命令不要和 app 同时刷新。M2 新增 audio 子命令，覆盖：
 *   info/rec/play/tone/gain/gen/dump/list/analyze/soak/selftest
 *
 * SPDX-License-Identifier: MIT
 */
#include "bench.h"

#include <dirent.h>
#include <inttypes.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_chip_info.h"
#include "esp_console.h"
#include "esp_heap_caps.h"
#include "esp_idf_version.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "sdkconfig.h"

#include "audio.h"
#include "audio_analysis.h"
#include "audio_common.h"
#include "audio_in.h"
#include "audio_out.h"
#include "audio_vad.h"
#include "audio_wav.h"
#include "board_pins.h"
#include "buttons.h"
#include "epd_lut_partial.h"
#include "epd_ssd1619a.h"
#include "storage.h"

static const char *TAG = "bench";

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* --------------------------------------------------------------- helpers */

static const char *reset_reason_name(esp_reset_reason_t r)
{
    switch (r) {
    case ESP_RST_POWERON:  return "POWERON";
    case ESP_RST_EXT:      return "EXT";
    case ESP_RST_SW:       return "SW";
    case ESP_RST_PANIC:    return "PANIC";
    case ESP_RST_INT_WDT:  return "INT_WDT";
    case ESP_RST_TASK_WDT: return "TASK_WDT";
    case ESP_RST_WDT:      return "WDT";
    case ESP_RST_DEEPSLEEP:return "DEEPSLEEP";
    case ESP_RST_BROWNOUT: return "BROWNOUT";
    case ESP_RST_SDIO:     return "SDIO";
    default:               return "UNKNOWN";
    }
}

static void draw_centered(uint8_t *fb, int y, const char *text, int scale)
{
    int w = epd_fb_text_width_scaled(text, scale);
    epd_fb_text_scaled(fb, (EPD_WIDTH - w) / 2, y, text, 0, scale);
}

static void draw_bench_base(uint8_t *fb)
{
    epd_fb_clear(fb, 1);
    epd_fb_rect(fb, 0, 0, EPD_WIDTH, EPD_HEIGHT, 0);
    draw_centered(fb, 30, "BENCH", 3);
    draw_centered(fb, 185, "M0 / EPD + FS + KEYS", 2);
    draw_centered(fb, 265, "bench> help", 1);
}

static int cmd_bench_info(void)
{
    esp_chip_info_t chip;
    esp_chip_info(&chip);

    printf("\n== bench info ==\n");
    printf("chip: ESP32-S3, cores=%d, revision=%d\n", chip.cores, chip.revision);
    printf("IDF: %s\n", esp_get_idf_version());
    printf("uptime: %lld s\n", (long long)(esp_timer_get_time() / 1000000));
    printf("reset: %s (%d)\n", reset_reason_name(esp_reset_reason()),
           (int)esp_reset_reason());
    printf("heap internal: free=%u total=%u\n",
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
           (unsigned)heap_caps_get_total_size(MALLOC_CAP_INTERNAL));
    printf("heap PSRAM:    free=%u total=%u\n",
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
           (unsigned)heap_caps_get_total_size(MALLOC_CAP_SPIRAM));
    printf("storage: %s\n", storage_is_mounted() ? "mounted" : "NOT mounted");
    return 0;
}

static int cmd_bench_fs(void)
{
    printf("\n== bench fs ==\n");
    esp_err_t a = storage_info();
    esp_err_t b = storage_selftest();
    printf("fs: info=%s selftest=%s\n", esp_err_to_name(a), esp_err_to_name(b));
    return (a == ESP_OK && b == ESP_OK) ? 0 : 1;
}

static int cmd_bench_display_band(int y0, int h, int n)
{
    if (y0 < 0 || h <= 0 || y0 + h > EPD_HEIGHT || n <= 0 || n > 100) {
        printf("usage: bench display band <y0> <h> <n>  (0<=y0, y0+h<=300, 1<=n<=100)\n");
        return 1;
    }

    static uint8_t fb[EPD_FRAME_BYTES];
    draw_bench_base(fb);

    int64_t min_us = INT64_MAX;
    int64_t max_us = 0;
    int64_t sum_us = 0;
    bool all_ok = true;

    printf("\n== band refresh y=%d h=%d n=%d ==\n", y0, h, n);
    for (int i = 0; i < n; i++) {
        char line[24];
        snprintf(line, sizeof(line), "BAND %d", i);
        draw_bench_base(fb);  /* clear previous text inside the band */
        draw_centered(fb, y0 + (h - 8 * 4) / 2, line, 4);

        int64_t t0 = esp_timer_get_time();
        bool ok = epd_band_update(fb, y0, y0 + h - 1, 5000);
        int64_t dt = esp_timer_get_time() - t0;
        if (dt < min_us) min_us = dt;
        if (dt > max_us) max_us = dt;
        sum_us += dt;
        all_ok = all_ok && ok;
        printf("  #%02d %s %lld ms\n", i, ok ? "ok" : "FAIL", (long long)(dt / 1000));
    }

    printf("band: min=%lld ms avg=%lld ms max=%lld ms\n",
           (long long)(min_us / 1000), (long long)(sum_us / n / 1000),
           (long long)(max_us / 1000));
    return all_ok ? 0 : 1;
}

static int cmd_bench_display_full(void)
{
    static uint8_t fb[EPD_FRAME_BYTES];
    draw_bench_base(fb);
    int64_t t0 = esp_timer_get_time();
    bool ok = epd_partial_show_full(fb, 5000);
    int64_t dt = esp_timer_get_time() - t0;
    printf("\nfull refresh: %s %lld ms\n", ok ? "ok" : "FAIL", (long long)(dt / 1000));
    return ok ? 0 : 1;
}

static int cmd_bench_display_clean(void)
{
    static uint8_t fb[EPD_FRAME_BYTES];
    printf("\n== clean cycle ==\n");

    bool all_ok = true;
    int64_t t0 = esp_timer_get_time();
    epd_fb_clear(fb, 1);
    bool ok = epd_partial_show_full(fb, 5000);
    all_ok = all_ok && ok;
    printf("  white: %s %lld ms\n", ok ? "ok" : "FAIL",
           (long long)((esp_timer_get_time() - t0) / 1000));

    t0 = esp_timer_get_time();
    epd_fb_clear(fb, 0);
    ok = epd_partial_show_full(fb, 5000);
    all_ok = all_ok && ok;
    printf("  black: %s %lld ms\n", ok ? "ok" : "FAIL",
           (long long)((esp_timer_get_time() - t0) / 1000));

    t0 = esp_timer_get_time();
    draw_bench_base(fb);
    ok = epd_partial_show_full(fb, 5000);
    all_ok = all_ok && ok;
    printf("  base:  %s %lld ms\n", ok ? "ok" : "FAIL",
           (long long)((esp_timer_get_time() - t0) / 1000));

    bool lut = epd_partial_reload_lut(epd_lut_partial_70);
    all_ok = all_ok && lut;
    printf("  LUT reload: %s\n", lut ? "ok" : "FAIL");
    return all_ok ? 0 : 1;
}

static int cmd_bench_keys(int seconds)
{
    if (seconds < 1 || seconds > 30) {
        printf("usage: bench keys [seconds]  (1..30)\n");
        return 1;
    }
    printf("\n== keys: press for %d s ==\n", seconds);
    printf("start mask=0x%02x (bit0=PREV bit1=NEXT bit2=PLAY bit3=RECORD bit4=SYNC)\n",
           buttons_mask());
    int64_t end = esp_timer_get_time() + (int64_t)seconds * 1000000;
    while (esp_timer_get_time() < end) {
        uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
        button_event_t ev = buttons_poll(now_ms);
        if (ev != BUTTON_EVENT_NONE) {
            printf("  key: %-12s mask=0x%02x\n", buttons_event_name(ev), buttons_mask());
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    printf("keys: done\n");
    return 0;
}

static int cmd_bench_selftest(void)
{
    printf("\n===== bench selftest =====\n");
    int rc = 0;
    rc |= cmd_bench_info();
    rc |= cmd_bench_fs();
    rc |= cmd_bench_display_band(95, 70, 3);
    rc |= cmd_bench_display_full();
    printf("===== selftest: %s =====\n", rc == 0 ? "PASS" : "FAIL");
    return rc;
}

/* --------------------------------------------------------------- audio */

/*
 * audio 子命令分工：
 *   info    只读状态，不碰 I2S；
 *   rec     录一段并打印峰值/RMS/VAD，是否顺便播放由第二个参数决定；
 *   play    播设备上的 WAV，默认播最新；
 *   tone    生成 1 kHz 正弦，绕过 WAV 文件，专门测功放；
 *   gain    运行时改数字增益（50..800），不写 NVS，重启恢复默认；
 *   gen     在设备内生成 1 kHz WAV，用于验证分析算法本身；
 *   dump    把 PCM 以 16 进制文本吐到串口，PC 工具再转 WAV；
 *   soak    连续录 N 条并逐条校验文件完整性；
 *   selftest 串起 info + tone + rec，不验证喇叭是否真的出声。
 */
static int cmd_bench_audio_info(void)
{
    printf("\n== bench audio info ==\n");
    printf("format: %d Hz / %d-bit / mono, rec default=%d ms\n",
           AUDIO_SAMPLE_RATE, AUDIO_BITS_PER_SAMPLE,
           CONFIG_APP_AUDIO_RECORD_MS);
    printf("mic:  I2S0 BCLK=%d WS=%d SD=%d pwr_gpio=%d gain=%d%%\n",
           BOARD_MIC_BCLK_GPIO, BOARD_MIC_WS_GPIO, BOARD_MIC_SD_GPIO,
           CONFIG_APP_AUDIO_MIC_POWER_GPIO, CONFIG_APP_AUDIO_MIC_GAIN_X100);
#ifdef CONFIG_APP_AUDIO_AMP_SD_ACTIVE_HIGH
    const char *amp_pol = "active high";
#else
    const char *amp_pol = "active low";
#endif
    printf("amp:  I2S1 BCLK=%d LRC=%d DIN=%d SD=%d (%s)\n",
           BOARD_AMP_BCLK_GPIO, BOARD_AMP_LRC_GPIO, BOARD_AMP_DIN_GPIO,
           BOARD_AMP_SD_GPIO, amp_pol);
    printf("play gain: %u%% (limiter on)\n", audio_out_get_gain_x100());
    printf("state: rec=%s play=%s busy=%s\n",
           audio_in_is_active() ? "active" : "idle",
           audio_out_is_playing() ? "playing" : "idle",
           audio_busy() ? "yes" : "no");
    printf("storage free: %u KB\n", (unsigned)(storage_free_bytes() / 1024));

    char path[160];
    if (audio_wav_latest(path, sizeof(path)) == ESP_OK) {
        wav_info_t info;
        if (audio_wav_info(path, &info) == ESP_OK) {
            printf("latest: %s (%" PRIu32 " ms, %" PRIu32 " frames)\n",
                   path, info.duration_ms, info.frames);
        } else {
            printf("latest: %s (unreadable)\n", path);
        }
    } else {
        printf("latest: none\n");
    }
    return 0;
}

/* 录一段：轮询电平直到 done，再 stop/分析/写 WAV；可选立即回放。 */
static int cmd_bench_audio_rec(int ms, bool play)
{
    if (ms < AUDIO_RECORD_MS_MIN || ms > AUDIO_RECORD_MS_MAX) {        printf("usage: bench audio rec [ms] [play]  (%d..%d)\n",
               AUDIO_RECORD_MS_MIN, AUDIO_RECORD_MS_MAX);
        return 1;
    }
    if (audio_busy()) {
        printf("audio busy (recording or playing)\n");
        return 1;
    }

    printf("\n== audio rec %d ms ==\n", ms);
    esp_err_t err = audio_in_start((uint32_t)ms);
    if (err != ESP_OK) {
        printf("start failed: %s\n", esp_err_to_name(err));
        return 1;
    }

    int64_t t0 = esp_timer_get_time();
    int last_level = -1;
    while (!audio_in_is_done()) {
        int level = audio_in_level_permille();
        if (level / 100 != last_level / 100) {
            printf("  rec %4" PRIu32 " ms  level=%3d%%\n",
                   audio_in_elapsed_ms(), level / 10);
            last_level = level;
        }
        if ((esp_timer_get_time() - t0) / 1000 > (int64_t)ms + 3000) {
            printf("  timeout waiting for capture task\n");
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    int16_t *pcm = NULL;
    size_t frames = 0;
    audio_capture_stats_t st;
    err = audio_in_stop(&pcm, &frames, &st);
    if (err != ESP_OK) {
        printf("stop failed: %s\n", esp_err_to_name(err));
        return 1;
    }

    if (frames == 0) {
        printf("no samples captured (mic not connected?)\n");
        audio_in_free(pcm);
        return 1;
    }

    audio_measure_t m;
    audio_measure(pcm, frames, &m);
    audio_vad_result_t vad;
    audio_vad_analyze(pcm, frames, &vad);

    char dur[16];
    printf("captured: %u frames (%s), start=%" PRIu32 " ms\n",
           (unsigned)frames, audio_wav_duration_str((uint32_t)frames, dur, sizeof(dur)),
           st.start_latency_ms);
    printf("level: peak=%u rms=%u dc=%d clip=%" PRIu32 " (%.2f%%)\n",
           st.peak, st.rms, st.dc, st.clipped, m.clip_ratio * 100.0f);
    printf("tone:  %.0f Hz, %.1f dBFS, SNR=%.1f dB\n",
           m.tone_hz, m.tone_dbfs, m.tone_snr_db);
    printf("vad:   speech=%s %" PRIu32 " ms, noise=%.1f dBFS, speech=%.1f dBFS, SNR=%.1f dB\n",
           vad.has_speech ? "yes" : "NO", vad.speech_ms,
           vad.noise_dbfs, vad.speech_dbfs, vad.snr_db);

    char path[160];
    err = audio_wav_make_rec_path(path, sizeof(path));
    if (err == ESP_OK) {
        err = audio_wav_write(path, pcm, frames);
    }
    if (err == ESP_OK) {
        printf("saved: %s\n", path);
    } else {
        printf("save failed: %s\n", esp_err_to_name(err));
    }

    if (play && err == ESP_OK) {
        printf("playback: %s\n", path);
        esp_err_t perr = audio_out_play_wav(path);
        printf("playback: %s\n", perr == ESP_OK ? "ok" : esp_err_to_name(perr));
    }

    audio_in_free(pcm);
    return err == ESP_OK ? 0 : 1;
}

static int cmd_bench_audio_play(const char *path)
{
    char latest[160];
    if (path == NULL) {
        if (audio_wav_latest(latest, sizeof(latest)) != ESP_OK) {
            printf("no recording found under %s\n", AUDIO_REC_DIR);
            return 1;
        }
        path = latest;
    }
    wav_info_t info;
    esp_err_t err = audio_wav_info(path, &info);
    if (err != ESP_OK) {
        printf("cannot read %s: %s\n", path, esp_err_to_name(err));
        return 1;
    }
    printf("\nplay %s: %" PRIu32 " ms, %" PRIu32 " frames\n",
           path, info.duration_ms, info.frames);
    err = audio_out_play_wav(path);
    printf("play: %s\n", err == ESP_OK ? "ok" : esp_err_to_name(err));
    return err == ESP_OK ? 0 : 1;
}

static int cmd_bench_audio_tone(int ms)
{
    if (ms < 100 || ms > 5000) {
        printf("usage: bench audio tone [ms]  (100..5000)\n");
        return 1;
    }
    printf("\ntone: 1000 Hz %d ms, amplitude=%d\n", ms,
           CONFIG_APP_AUDIO_TONE_AMPLITUDE);
    esp_err_t err = audio_out_play_tone(1000, (uint32_t)ms,
                                        CONFIG_APP_AUDIO_TONE_AMPLITUDE);
    printf("tone: %s\n", err == ESP_OK ? "ok" : esp_err_to_name(err));
    return err == ESP_OK ? 0 : 1;
}

/* 生成 1 kHz WAV 并用同一套 audio_measure 分析，验证算法而非硬件。 */
static int cmd_bench_audio_gen(int ms)
{
    if (ms < 100 || ms > 5000) {
        printf("usage: bench audio gen [ms]\n");
        return 1;
    }
    size_t frames = (size_t)((uint64_t)AUDIO_SAMPLE_RATE * ms / 1000);
    int16_t *pcm = heap_caps_malloc(frames * sizeof(int16_t),
                                    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (pcm == NULL) {
        pcm = malloc(frames * sizeof(int16_t));
    }
    if (pcm == NULL) {
        return 1;
    }
    for (size_t i = 0; i < frames; i++) {
        float phase = 2.0f * (float)M_PI * 1000.0f * (float)i /
                      (float)AUDIO_SAMPLE_RATE;
        pcm[i] = (int16_t)(sinf(phase) * 6000.0f);
    }

    const char *path = AUDIO_REC_DIR "/gen_1khz.wav";
    esp_err_t err = audio_wav_write(path, pcm, frames);
    audio_measure_t m;
    audio_measure(pcm, frames, &m);
    printf("\ngen: %s, frames=%u\n", path, (unsigned)frames);
    printf("  expected: 1000 Hz, -14.7 dBFS\n");
    printf("  measured: %.0f Hz, %.1f dBFS, SNR=%.1f dB, rms=%.1f dBFS, dc=%d\n",
           m.tone_hz, m.tone_dbfs, m.tone_snr_db, m.rms_dbfs, m.dc);
    audio_in_free(pcm);
    return err == ESP_OK ? 0 : 1;
}

/* dump 用 %04x 输出“数值”的十六进制；PC 端必须按 int16 解析后
 * little-endian 写 WAV，不能直接 bytes.fromhex()。 */
static int cmd_bench_audio_dump(const char *path)
{
    if (path == NULL) {
        printf("usage: bench audio dump <file>\n");
        return 1;
    }
    int16_t *pcm = NULL;
    size_t frames = 0;
    esp_err_t err = audio_wav_read(path, &pcm, &frames);
    if (err != ESP_OK) {
        printf("read %s failed: %s\n", path, esp_err_to_name(err));
        return 1;
    }
    printf("DUMP %u\n", (unsigned)frames);
    for (size_t i = 0; i < frames; i += 16) {
        for (size_t j = 0; j < 16 && i + j < frames; j++) {
            printf("%04x", (uint16_t)pcm[i + j]);
        }
        printf("\n");
    }
    printf("ENDDUMP\n");
    audio_in_free(pcm);
    return 0;
}

static int cmd_bench_audio_gain(int argc, char **argv)
{
    if (argc < 4) {
        printf("play gain: %u%%\n", audio_out_get_gain_x100());
        return 0;
    }
    int gain = atoi(argv[3]);
    esp_err_t err = audio_out_set_gain_x100((uint16_t)gain);
    if (err != ESP_OK) {
        printf("usage: bench audio gain <50..800>\n");
        return 1;
    }
    printf("play gain: %u%% (limiter on)\n", audio_out_get_gain_x100());
    return 0;
}

/* 列出 /storage/rec 下的 WAV，逐个读头显示时长/大小。 */
static int cmd_bench_audio_list(void)
{
    DIR *dir = opendir(AUDIO_REC_DIR);
    if (dir == NULL) {
        printf("no directory %s\n", AUDIO_REC_DIR);
        return 1;
    }
    printf("\n== %s ==\n", AUDIO_REC_DIR);
    int count = 0;
    uint64_t total = 0;
    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        size_t n = strlen(ent->d_name);
        if (n < 5 || strcmp(ent->d_name + n - 4, ".wav") != 0) {
            continue;
        }
        char path[320];
        snprintf(path, sizeof(path), "%s/%s", AUDIO_REC_DIR, ent->d_name);
        struct stat st;
        if (stat(path, &st) != 0) {
            continue;
        }
        wav_info_t info;
        if (audio_wav_info(path, &info) == ESP_OK) {
            printf("  %-34s %6" PRIu32 " ms %7u B\n", ent->d_name,
                   info.duration_ms, (unsigned)st.st_size);
        } else {
            printf("  %-34s (bad WAV) %7u B\n", ent->d_name,
                   (unsigned)st.st_size);
        }
        count++;
        total += (uint64_t)st.st_size;
    }
    closedir(dir);
    printf("files=%d total=%" PRIu64 " KB\n", count, total / 1024);
    return count > 0 ? 0 : 1;
}

/* 对已有 WAV 做电平/1 kHz/VAD 分析，不播放。 */
static int cmd_bench_audio_analyze(const char *path)
{
    char latest[160];
    if (path == NULL) {
        if (audio_wav_latest(latest, sizeof(latest)) != ESP_OK) {
            printf("no recording found\n");
            return 1;
        }
        path = latest;
    }
    int16_t *pcm = NULL;
    size_t frames = 0;
    esp_err_t err = audio_wav_read(path, &pcm, &frames);
    if (err != ESP_OK) {
        printf("read %s failed: %s\n", path, esp_err_to_name(err));
        return 1;
    }
    audio_measure_t m;
    audio_measure(pcm, frames, &m);
    audio_vad_result_t vad;
    audio_vad_analyze(pcm, frames, &vad);
    printf("\n%s\n", path);
    printf("  frames=%u peak=%d rms=%.1f dBFS dc=%d clip=%" PRIu32 " (%.2f%%)\n",
           (unsigned)frames, m.peak, m.rms_dbfs, m.dc, m.clipped,
           m.clip_ratio * 100.0f);
    printf("  tone=%.0f Hz %.1f dBFS SNR=%.1f dB\n",
           m.tone_hz, m.tone_dbfs, m.tone_snr_db);
    printf("  vad=%s speech=%" PRIu32 " ms noise=%.1f speech=%.1f snr=%.1f zcr=%.3f\n",
           vad.has_speech ? "yes" : "NO", vad.speech_ms,
           vad.noise_dbfs, vad.speech_dbfs, vad.snr_db, vad.zcr);
    audio_in_free(pcm);
    return 0;
}

/* soak：每条 1 s，录完立刻用 audio_wav_info 重新读头校验帧数，避免
 * “写成功但文件损坏”被漏掉。 */
static int cmd_bench_audio_soak(int n)
{
    if (n < 1 || n > 30) {
        printf("usage: bench audio soak [n]  (1..30)\n");
        return 1;
    }
    printf("\n== audio soak %d x 1000 ms ==\n", n);
    int ok = 0;
    for (int i = 0; i < n; i++) {
        esp_err_t err = audio_in_start(1000);
        if (err != ESP_OK) {
            printf("  #%02d start failed: %s\n", i, esp_err_to_name(err));
            continue;
        }
        int64_t wait_start = esp_timer_get_time();
        while (!audio_in_is_done() &&
               (esp_timer_get_time() - wait_start) < 4000000) {
            vTaskDelay(pdMS_TO_TICKS(20));
        }
        if (!audio_in_is_done()) {
            printf("  #%02d capture timeout\n", i);
            audio_in_cancel();
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        int16_t *pcm = NULL;
        size_t frames = 0;
        audio_capture_stats_t st;
        err = audio_in_stop(&pcm, &frames, &st);
        if (err != ESP_OK || frames == 0) {
            printf("  #%02d stop=%s frames=%u\n", i, esp_err_to_name(err),
                   (unsigned)frames);
            audio_in_free(pcm);
            continue;
        }
        audio_vad_result_t vad;
        audio_vad_analyze(pcm, frames, &vad);
        char path[160];
        err = audio_wav_make_rec_path(path, sizeof(path));
        if (err == ESP_OK) {
            err = audio_wav_write(path, pcm, frames);
        }
        wav_info_t info;
        bool valid = (err == ESP_OK) &&
                     (audio_wav_info(path, &info) == ESP_OK) &&
                     (info.frames == frames);
        printf("  #%02d frames=%u clip=%" PRIu32 " speech=%s %" PRIu32
               " ms file=%s\n",
               i, (unsigned)frames, st.clipped,
               vad.has_speech ? "yes" : "NO", vad.speech_ms,
               valid ? "OK" : "FAIL");
        if (valid) {
            ok++;
        }
        audio_in_free(pcm);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    printf("soak: %d/%d valid files\n", ok, n);
    return ok == n ? 0 : 1;
}

/* selftest 只覆盖软件链；喇叭是否出声要用 phone 1 kHz + analyze 或人耳。 */
static int cmd_bench_audio_selftest(void)
{
    printf("\n===== audio selftest =====\n");
    int rc = cmd_bench_audio_info();
    rc |= cmd_bench_audio_tone(300);
    rc |= cmd_bench_audio_rec(1000, false);
    printf("===== audio selftest: %s =====\n", rc == 0 ? "PASS" : "FAIL");
    return rc;
}

static int cmd_bench_audio(int argc, char **argv)
{
    if (argc < 3) {
        printf("usage: bench audio info|rec|play|tone|gain|gen|list|analyze|soak|selftest\n");
        return 1;
    }
    if (!strcmp(argv[2], "info")) {
        return cmd_bench_audio_info();
    }
    if (!strcmp(argv[2], "rec")) {
        int ms = (argc >= 4) ? atoi(argv[3]) : CONFIG_APP_AUDIO_RECORD_MS;
        bool play = (argc >= 5 && !strcmp(argv[4], "play"));
        return cmd_bench_audio_rec(ms, play);
    }
    if (!strcmp(argv[2], "play")) {
        return cmd_bench_audio_play(argc >= 4 ? argv[3] : NULL);
    }
    if (!strcmp(argv[2], "tone")) {
        int ms = (argc >= 4) ? atoi(argv[3]) : 1000;
        return cmd_bench_audio_tone(ms);
    }
    if (!strcmp(argv[2], "gain")) {
        return cmd_bench_audio_gain(argc, argv);
    }
    if (!strcmp(argv[2], "gen")) {
        int ms = (argc >= 4) ? atoi(argv[3]) : 500;
        return cmd_bench_audio_gen(ms);
    }
    if (!strcmp(argv[2], "dump")) {
        return cmd_bench_audio_dump(argc >= 4 ? argv[3] : NULL);
    }
    if (!strcmp(argv[2], "list")) {
        return cmd_bench_audio_list();
    }
    if (!strcmp(argv[2], "analyze")) {
        return cmd_bench_audio_analyze(argc >= 4 ? argv[3] : NULL);
    }
    if (!strcmp(argv[2], "soak")) {
        int n = (argc >= 4) ? atoi(argv[3]) : 20;
        return cmd_bench_audio_soak(n);
    }
    if (!strcmp(argv[2], "selftest")) {
        return cmd_bench_audio_selftest();
    }
    printf("unknown audio subcommand: %s\n", argv[2]);
    return 1;
}

static void print_bench_usage(void)
{
    printf("usage:\n");
    printf("  bench info\n");
    printf("  bench fs\n");
    printf("  bench display band <y0> <h> <n>\n");
    printf("  bench display full\n");
    printf("  bench display clean\n");
    printf("  bench keys [seconds]\n");
    printf("  bench audio info|rec [ms] [play]|play [file]|tone [ms]\n");
    printf("               list|analyze [file]|soak [n]|selftest\n");
    printf("  bench selftest\n");
}

static int cmd_bench(int argc, char **argv)
{
    if (argc < 2) {
        print_bench_usage();
        return 0;
    }

    if (!strcmp(argv[1], "info")) {
        return cmd_bench_info();
    }
    if (!strcmp(argv[1], "fs")) {
        return cmd_bench_fs();
    }
    if (!strcmp(argv[1], "keys")) {
        int sec = (argc >= 3) ? atoi(argv[2]) : 5;
        return cmd_bench_keys(sec);
    }
    if (!strcmp(argv[1], "selftest")) {
        return cmd_bench_selftest();
    }
    if (!strcmp(argv[1], "audio")) {
        return cmd_bench_audio(argc, argv);
    }
    if (!strcmp(argv[1], "display")) {
        if (argc < 3) {
            print_bench_usage();
            return 1;
        }
        if (!strcmp(argv[2], "band")) {
            if (argc < 6) {
                print_bench_usage();
                return 1;
            }
            return cmd_bench_display_band(atoi(argv[3]), atoi(argv[4]), atoi(argv[5]));
        }
        if (!strcmp(argv[2], "full")) {
            return cmd_bench_display_full();
        }
        if (!strcmp(argv[2], "clean")) {
            return cmd_bench_display_clean();
        }
        printf("unknown display subcommand: %s\n", argv[2]);
        return 1;
    }

    printf("unknown bench subcommand: %s\n", argv[1]);
    print_bench_usage();
    return 1;
}

esp_err_t bench_init(void)
{
    esp_console_repl_t *repl = NULL;

    esp_console_repl_config_t repl_cfg = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_cfg.prompt = "bench>";
    repl_cfg.max_cmdline_length = 128;

    esp_console_dev_uart_config_t uart_cfg = {
        .channel = 0,
        .baud_rate = 115200,
        .tx_gpio_num = -1,
        .rx_gpio_num = -1,
    };

    esp_err_t err = esp_console_new_repl_uart(&uart_cfg, &repl_cfg, &repl);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_console_new_repl_uart failed: %s", esp_err_to_name(err));
        return err;
    }

    const esp_console_cmd_t cmd = {
        .command = "bench",
        .help = "E-Ink board bring-up commands",
        .hint = "info | fs | display | keys | audio | selftest",
        .func = cmd_bench,
    };
    err = esp_console_cmd_register(&cmd);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "register bench failed: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_console_register_help_command();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "register help failed: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_console_start_repl(repl);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "start repl failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "console started on UART0 115200; type 'bench help'");
    return ESP_OK;
}
