/*
 * collect.c — 设备端数据采集模式（M4 准备版）。
 *
 * 目标：不依赖 PC 麦克风，直接用板载 MS4030 录 2 s 跟读，把
 * speaker/word/label/take 写进 CSV，WAV 落到 /storage/collect/audio/。
 *
 * 按键：
 *   PREV/NEXT  上一个/下一个单词（切词时 take 回到 1）
 *   SYNC 短按  正确/错误标签翻转（L1/L0）
 *   SYNC 长按  切换说话人 S01..S08
 *   RECORD     录 2 s，VAD 通过才保存
 *   PLAY       回放最后一条
 *
 * 串口（bench collect ...）给 PC 工具用：
 *   info / set <S01> <word> <0|1> / last / rec / list / csv / delete <path>
 *
 * 数据格式对齐 tools/pipeline/dataset.py：
 *   utt_id,file,speaker_id,word_id,label,error_type,notes,split,take,ts
 *
 * SPDX-License-Identifier: MIT
 */
#include "collect.h"

#include <dirent.h>
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs.h"
#include "sdkconfig.h"

#include "audio_common.h"
#include "audio_in.h"
#include "audio_out.h"
#include "audio_vad.h"
#include "audio_wav.h"
#include "boot_clean.h"
#include "buttons.h"
#include "epd_ssd1619a.h"
#include "storage.h"
#include "word_view.h"

static const char *TAG = "collect";

#define COLLECT_DIR        "/storage/collect"
#define COLLECT_AUDIO_DIR  "/storage/collect/audio"
#define COLLECT_CSV        "/storage/collect/labels.csv"
#define COLLECT_NVS_NS     "collect"
#define COLLECT_MAX_SPK    8
#define COLLECT_MAX_TAKE   99
#define COLLECT_LOW_SPACE  (128 * 1024)

static uint8_t s_base[EPD_FRAME_BYTES];
static uint8_t s_frame[EPD_FRAME_BYTES];

static uint8_t s_speaker = 1;
static uint8_t s_word = 0;
static uint8_t s_label = 1;
static uint8_t s_take = 1;
static bool s_recording;
static bool s_has_last;
static char s_last_path[160];
static char s_last_utt[96];
static uint32_t s_next_ui_ms;

static volatile bool s_record_req;
static volatile bool s_ui_dirty;

static nvs_handle_t s_nvs;
static bool s_nvs_open;

static void draw_centered(uint8_t *fb, int y, const char *text, int scale)
{
    int w = epd_fb_text_width_scaled(text, scale);
    epd_fb_text_scaled(fb, (EPD_WIDTH - w) / 2, y, text, 0, scale);
}

static esp_err_t ensure_dir(const char *path)
{
    struct stat st;
    if (stat(path, &st) == 0) {
        return S_ISDIR(st.st_mode) ? ESP_OK : ESP_FAIL;
    }
    if (mkdir(path, 0775) == 0 || errno == EEXIST) {
        return ESP_OK;
    }
    ESP_LOGE(TAG, "mkdir(%s) failed: errno=%d", path, errno);
    return ESP_FAIL;
}

static void save_state(void)
{
    if (!s_nvs_open) {
        return;
    }
    (void)nvs_set_u8(s_nvs, "speaker", s_speaker);
    (void)nvs_set_u8(s_nvs, "word", s_word);
    (void)nvs_set_u8(s_nvs, "label", s_label);
    (void)nvs_set_u8(s_nvs, "take", s_take);
    (void)nvs_commit(s_nvs);
}

static void load_state(void)
{
    esp_err_t err = nvs_open(COLLECT_NVS_NS, NVS_READWRITE, &s_nvs);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "nvs_open failed: %s (using defaults)", esp_err_to_name(err));
        return;
    }
    s_nvs_open = true;

    uint8_t v = 0;
    if (nvs_get_u8(s_nvs, "speaker", &v) == ESP_OK && v >= 1 && v <= COLLECT_MAX_SPK) {
        s_speaker = v;
    }
    if (nvs_get_u8(s_nvs, "word", &v) == ESP_OK && v < word_view_count()) {
        s_word = v;
    }
    if (nvs_get_u8(s_nvs, "label", &v) == ESP_OK && v <= 1) {
        s_label = v;
    }
    if (nvs_get_u8(s_nvs, "take", &v) == ESP_OK && v >= 1 && v <= COLLECT_MAX_TAKE) {
        s_take = v;
    }
}

static void refresh_last(void)
{
    s_has_last = false;
    s_last_path[0] = 0;
    s_last_utt[0] = 0;

    DIR *dir = opendir(COLLECT_AUDIO_DIR);
    if (dir == NULL) {
        return;
    }
    char best[128] = {0};
    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        size_t n = strlen(ent->d_name);
        if (n < 5 || strcmp(ent->d_name + n - 4, ".wav") != 0) {
            continue;
        }
        if (best[0] == 0 || strcmp(ent->d_name, best) > 0) {
            snprintf(best, sizeof(best), "%.*s", (int)(sizeof(best) - 1), ent->d_name);
        }
    }
    closedir(dir);
    if (best[0] == 0) {
        return;
    }
    snprintf(s_last_path, sizeof(s_last_path), "%s/%s", COLLECT_AUDIO_DIR, best);
    snprintf(s_last_utt, sizeof(s_last_utt), "%.*s", (int)(strlen(best) - 4), best);
    s_has_last = true;
}

static void render_base(void)
{
    epd_fb_clear(s_base, 1);
    epd_fb_rect(s_base, 0, 0, EPD_WIDTH, EPD_HEIGHT, 0);
    draw_centered(s_base, 24, "COLLECT", 3);
    draw_centered(s_base, 242, "SYNC=LBL  HOLD SYNC=SPK", 1);
    draw_centered(s_base, 260, "PREV/NEXT  REC  PLAY", 1);
}

static void show_ready(void)
{
    memcpy(s_frame, s_base, EPD_FRAME_BYTES);
    char meta[40];
    snprintf(meta, sizeof(meta), "S%02u W%02u L%u T%02u",
             s_speaker, s_word, s_label, s_take);
    draw_centered(s_frame, 96, meta, 2);
    draw_centered(s_frame, 126, word_view_word(s_word), 4);
    epd_band_update(s_frame, 95, 164, 5000);
}

static void show_status(const char *text, int scale)
{
    memcpy(s_frame, s_base, EPD_FRAME_BYTES);
    int y = 95 + (70 - 8 * scale) / 2;
    draw_centered(s_frame, y, text, scale);
    epd_band_update(s_frame, 95, 164, 5000);
}

static void show_record(uint32_t elapsed_ms, int level_permille)
{
    memcpy(s_frame, s_base, EPD_FRAME_BYTES);
    char line[24];
    snprintf(line, sizeof(line), "REC %u.%u s",
             (unsigned)(elapsed_ms / 1000),
             (unsigned)((elapsed_ms % 1000) / 100));
    draw_centered(s_frame, 100, line, 2);

    epd_fb_rect(s_frame, 80, 138, 240, 16, 0);
    int fill = level_permille * 236 / 1000;
    if (fill > 0) {
        epd_fb_fill_rect(s_frame, 82, 140, fill, 12, 0);
    }
    epd_band_update(s_frame, 95, 164, 5000);
}

static bool append_metadata(const char *utt, const char *rel_file,
                            uint32_t ts, uint8_t take)
{
    struct stat st;
    bool exists = (stat(COLLECT_CSV, &st) == 0 && st.st_size > 0);
    FILE *f = fopen(COLLECT_CSV, "a");
    if (f == NULL) {
        ESP_LOGE(TAG, "open %s failed", COLLECT_CSV);
        return false;
    }
    if (!exists) {
        fprintf(f, "utt_id,file,speaker_id,word_id,label,error_type,notes,split,take,ts\n");
    }
    fprintf(f, "%s,%s,S%02u,%u,%u,,device,,%u,%u\n",
            utt, rel_file, s_speaker, s_word, s_label, take, (unsigned)ts);
    fflush(f);
    (void)fsync(fileno(f));
    fclose(f);
    return true;
}

static void make_paths(char *path, size_t path_len, char *rel, size_t rel_len,
                       char *utt, size_t utt_len, uint32_t *ts_out)
{
    audio_wav_seed_clock();
    time_t now = time(NULL);
    struct timeval tv;
    gettimeofday(&tv, NULL);
    uint32_t ts = (uint32_t)now;
    unsigned ms = (unsigned)(tv.tv_usec / 1000);

    snprintf(utt, utt_len, "S%02u_W%02u_L%u_T%02u_%u%03u",
             s_speaker, s_word, s_label, s_take, (unsigned)ts, ms);
    snprintf(rel, rel_len, "audio/%s.wav", utt);
    snprintf(path, path_len, "%s/%s.wav", COLLECT_AUDIO_DIR, utt);
    *ts_out = ts;
}

static void finish_recording(void)
{
    int16_t *pcm = NULL;
    size_t frames = 0;
    audio_capture_stats_t stats;
    esp_err_t err = audio_in_stop(&pcm, &frames, &stats);
    s_recording = false;
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "stop failed: %s", esp_err_to_name(err));
        show_status("AUDIO ERR", 3);
        return;
    }
    if (frames == 0) {
        show_status("NO SAMPLES", 3);
        audio_in_free(pcm);
        return;
    }

    audio_vad_result_t vad;
    err = audio_vad_analyze(pcm, frames, &vad);
    if (err != ESP_OK || !vad.has_speech) {
        ESP_LOGW(TAG, "no speech: %.1f/%.1f dBFS snr=%.1f zcr=%.3f",
                 vad.noise_dbfs, vad.speech_dbfs, vad.snr_db, vad.zcr);
        show_status("NO SPEECH", 3);
        audio_in_free(pcm);
        vTaskDelay(pdMS_TO_TICKS(700));
        show_ready();
        return;
    }

    size_t n = vad.speech_end - vad.speech_start;
    if (n < AUDIO_SAMPLE_RATE / 4) {
        show_status("TOO SHORT", 3);
        audio_in_free(pcm);
        vTaskDelay(pdMS_TO_TICKS(700));
        show_ready();
        return;
    }
    if (storage_free_bytes() < (n * sizeof(int16_t) + 16384)) {
        show_status("FULL", 3);
        audio_in_free(pcm);
        vTaskDelay(pdMS_TO_TICKS(700));
        show_ready();
        return;
    }

    char path[160], rel[128], utt[96];
    uint32_t ts = 0;
    make_paths(path, sizeof(path), rel, sizeof(rel), utt, sizeof(utt), &ts);
    err = audio_wav_write(path, pcm + vad.speech_start, n);
    if (err == ESP_OK) {
        append_metadata(utt, rel, ts, s_take);
        snprintf(s_last_path, sizeof(s_last_path), "%s", path);
        snprintf(s_last_utt, sizeof(s_last_utt), "%s", utt);
        s_has_last = true;

        char dur[16];
        char line[32];
        audio_wav_duration_str((uint32_t)n, dur, sizeof(dur));
        snprintf(line, sizeof(line), "SAVED %s", dur);
        ESP_LOGI(TAG, "%s %s (%u frames, snr=%.1f dB)", utt, path,
                 (unsigned)n, vad.snr_db);
        if (s_take < COLLECT_MAX_TAKE) {
            s_take++;
        } else {
            s_take = 1;
        }
        save_state();
        show_status(line, 3);
        vTaskDelay(pdMS_TO_TICKS(700));
        show_ready();
    } else {
        ESP_LOGE(TAG, "write failed: %s", esp_err_to_name(err));
        show_status("SAVE ERR", 3);
    }
    audio_in_free(pcm);
}

static void start_recording(void)
{
    if (s_recording || audio_out_is_playing()) {
        return;
    }
    if (storage_free_bytes() < COLLECT_LOW_SPACE) {
        show_status("FULL", 3);
        return;
    }
    esp_err_t err = audio_in_start(CONFIG_APP_AUDIO_RECORD_MS);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "record start failed: %s", esp_err_to_name(err));
        show_status("AUDIO ERR", 3);
        return;
    }
    s_recording = true;
    s_next_ui_ms = 0;
    ESP_LOGI(TAG, "record S%02u W%02u L%u T%02u", s_speaker, s_word,
             s_label, s_take);
}

static void play_last(void)
{
    if (s_recording || audio_out_is_playing()) {
        return;
    }
    if (!s_has_last) {
        refresh_last();
    }
    if (!s_has_last) {
        show_status("NO REC", 3);
        return;
    }
    show_status("PLAYING", 3);
    esp_err_t err = audio_out_play_wav(s_last_path);
    show_status(err == ESP_OK ? "DONE" : "PLAY ERR", 3);
    vTaskDelay(pdMS_TO_TICKS(400));
    show_ready();
}

static void handle_key(button_event_t ev)
{
    if (s_recording) {
        return;
    }
    switch (ev) {
    case BUTTON_EVENT_PREV_SHORT:
        s_word = (s_word == 0) ? (uint8_t)(word_view_count() - 1) : (uint8_t)(s_word - 1);
        s_take = 1;
        save_state();
        show_ready();
        break;
    case BUTTON_EVENT_NEXT_SHORT:
        s_word = (uint8_t)((s_word + 1) % word_view_count());
        s_take = 1;
        save_state();
        show_ready();
        break;
    case BUTTON_EVENT_SYNC_SHORT:
        s_label ^= 1;
        save_state();
        show_ready();
        break;
    case BUTTON_EVENT_SYNC_LONG:
        s_speaker = (uint8_t)((s_speaker % COLLECT_MAX_SPK) + 1);
        s_take = 1;
        save_state();
        show_ready();
        break;
    case BUTTON_EVENT_RECORD_SHORT:
        start_recording();
        break;
    case BUTTON_EVENT_PLAY_SHORT:
        play_last();
        break;
    default:
        break;
    }
}

static void collect_init(void)
{
    (void)ensure_dir(COLLECT_DIR);
    (void)ensure_dir(COLLECT_AUDIO_DIR);
    load_state();
    refresh_last();
}

void collect_run(void)
{
    collect_init();
    render_base();

#if CONFIG_APP_BOOT_CLEAN
    if (boot_clean_should_run()) {
        boot_clean_run(s_base, s_frame, "collect");
    } else {
        memcpy(s_frame, s_base, EPD_FRAME_BYTES);
        epd_partial_show_full(s_frame, 5000);
    }
#else
    memcpy(s_frame, s_base, EPD_FRAME_BYTES);
    epd_partial_show_full(s_frame, 5000);
#endif

    show_ready();
    ESP_LOGI(TAG, "collect ready: S%02u W%02u L%u T%02u, files under %s",
             s_speaker, s_word, s_label, s_take, COLLECT_AUDIO_DIR);

    while (true) {
        uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
        button_event_t ev = buttons_poll(now_ms);

        if (ev != BUTTON_EVENT_NONE) {
            handle_key(ev);
        }
        if (s_ui_dirty) {
            s_ui_dirty = false;
            show_ready();
        }
        if (s_record_req) {
            s_record_req = false;
            start_recording();
        }
        if (s_recording) {
            if (audio_in_is_done()) {
                finish_recording();
            } else if ((int32_t)(now_ms - s_next_ui_ms) >= 0) {
                show_record(audio_in_elapsed_ms(), audio_in_level_permille());
                s_next_ui_ms = now_ms + 500;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void collect_request_record(void)
{
    s_record_req = true;
}

bool collect_set_state(int speaker, int word, int label)
{
    if (s_recording || speaker < 1 || speaker > COLLECT_MAX_SPK ||
        word < 0 || word >= word_view_count() || (label != 0 && label != 1)) {
        return false;
    }
    if (speaker != s_speaker || word != s_word) {
        s_take = 1;
    }
    s_speaker = (uint8_t)speaker;
    s_word = (uint8_t)word;
    s_label = (uint8_t)label;
    save_state();
    s_ui_dirty = true;
    return true;
}

void collect_info(void)
{
    int count = 0;
    uint64_t total = 0;
    DIR *dir = opendir(COLLECT_AUDIO_DIR);
    if (dir != NULL) {
        struct dirent *ent;
        while ((ent = readdir(dir)) != NULL) {
            size_t n = strlen(ent->d_name);
            if (n < 5 || strcmp(ent->d_name + n - 4, ".wav") != 0) {
                continue;
            }
            char path[320];
            snprintf(path, sizeof(path), "%s/%s", COLLECT_AUDIO_DIR, ent->d_name);
            struct stat st;
            if (stat(path, &st) == 0) {
                count++;
                total += (uint64_t)st.st_size;
            }
        }
        closedir(dir);
    }

    printf("collect: S%02u W%02u L%u T%02u\n", s_speaker, s_word, s_label, s_take);
    printf("files=%d total=%" PRIu64 " B free=%u KB\n", count, total,
           (unsigned)(storage_free_bytes() / 1024));
    printf("last=%s\n", s_has_last ? s_last_path : "none");
    printf("csv=%s\n", COLLECT_CSV);
}

void collect_list(void)
{
    collect_csv();
}

void collect_csv(void)
{
    FILE *f = fopen(COLLECT_CSV, "r");
    if (f == NULL) {
        printf("utt_id,file,speaker_id,word_id,label,error_type,notes,split,take,ts\n");
        return;
    }
    char line[256];
    while (fgets(line, sizeof(line), f) != NULL) {
        printf("%s", line);
    }
    fclose(f);
}

bool collect_delete(const char *path)
{
    const char *prefix = COLLECT_AUDIO_DIR "/";
    if (path == NULL || strncmp(path, prefix, strlen(prefix)) != 0) {
        printf("refuse: path must start with %s\n", prefix);
        return false;
    }
    if (remove(path) != 0) {
        printf("delete failed: %s\n", path);
        return false;
    }
    printf("deleted: %s\n", path);
    if (s_has_last && strcmp(path, s_last_path) == 0) {
        refresh_last();
    }
    return true;
}

const char *collect_last_path(void)
{
    return s_has_last ? s_last_path : "";
}

const char *collect_last_utt(void)
{
    return s_has_last ? s_last_utt : "";
}
