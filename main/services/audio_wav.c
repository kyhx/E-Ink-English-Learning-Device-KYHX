/*
 * audio_wav.c — minimal RIFF/WAVE reader and mono PCM16 writer.
 *
 * SPDX-License-Identifier: MIT
 */
#include "audio_wav.h"

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

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "audio_common.h"

static const char *TAG = "audio_wav";

/* ------------------------------------------------------------ clock seed */

static int month_index(const char *m)
{
    static const char *const names[] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec",
    };
    for (int i = 0; i < 12; i++) {
        if (strncmp(m, names[i], 3) == 0) {
            return i;
        }
    }
    return -1;
}

void audio_wav_seed_clock(void)
{
    time_t now = time(NULL);
    if (now >= 1700000000) {
        return;
    }

    char mon[4] = {0};
    int day = 0, year = 0, hh = 0, mm = 0, ss = 0;
    if (sscanf(__DATE__, "%3s %d %d", mon, &day, &year) != 3 ||
        sscanf(__TIME__, "%d:%d:%d", &hh, &mm, &ss) != 3) {
        return;
    }
    int mon_idx = month_index(mon);
    if (mon_idx < 0 || year < 2024) {
        return;
    }

    struct tm tm = {
        .tm_year = year - 1900,
        .tm_mon = mon_idx,
        .tm_mday = day,
        .tm_hour = hh,
        .tm_min = mm,
        .tm_sec = ss,
        .tm_isdst = -1,
    };
    time_t build = mktime(&tm);
    if (build <= 0) {
        return;
    }
    struct timeval tv = {.tv_sec = build, .tv_usec = 0};
    if (settimeofday(&tv, NULL) == 0) {
        ESP_LOGW(TAG, "no RTC/SNTP: clock seeded from build time (%04d-%02d-%02d %02d:%02d:%02d)",
                 year, mon_idx + 1, day, hh, mm, ss);
    }
}

/* --------------------------------------------------------------- helpers */

static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void wr16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)(v >> 8);
}

static void wr32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF);
    p[3] = (uint8_t)((v >> 24) & 0xFF);
}

static esp_err_t ensure_rec_dir(void)
{
    struct stat st;
    if (stat(AUDIO_REC_DIR, &st) == 0) {
        return S_ISDIR(st.st_mode) ? ESP_OK : ESP_FAIL;
    }
    if (mkdir(AUDIO_REC_DIR, 0775) == 0 || errno == EEXIST) {
        return ESP_OK;
    }
    ESP_LOGE(TAG, "mkdir(%s) failed: errno=%d", AUDIO_REC_DIR, errno);
    return ESP_FAIL;
}

esp_err_t audio_wav_init(void)
{
    audio_wav_seed_clock();
    return ensure_rec_dir();
}

/* ---------------------------------------------------------------- writer */

esp_err_t audio_wav_write(const char *path, const int16_t *pcm, size_t frames)
{
    if (path == NULL || pcm == NULL || frames == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = ensure_rec_dir();
    if (err != ESP_OK) {
        return err;
    }

    uint32_t data_bytes = (uint32_t)(frames * sizeof(int16_t));
    uint8_t hdr[44];
    memcpy(hdr, "RIFF", 4);
    wr32(hdr + 4, 36 + data_bytes);
    memcpy(hdr + 8, "WAVE", 4);
    memcpy(hdr + 12, "fmt ", 4);
    wr32(hdr + 16, 16);
    wr16(hdr + 20, 1);                       /* PCM */
    wr16(hdr + 22, AUDIO_CHANNELS);
    wr32(hdr + 24, AUDIO_SAMPLE_RATE);
    wr32(hdr + 28, AUDIO_SAMPLE_RATE * AUDIO_CHANNELS * (AUDIO_BITS_PER_SAMPLE / 8));
    wr16(hdr + 32, AUDIO_CHANNELS * (AUDIO_BITS_PER_SAMPLE / 8));
    wr16(hdr + 34, AUDIO_BITS_PER_SAMPLE);
    memcpy(hdr + 36, "data", 4);
    wr32(hdr + 40, data_bytes);

    FILE *f = fopen(path, "wb");
    if (f == NULL) {
        ESP_LOGE(TAG, "fopen(%s) failed", path);
        return ESP_FAIL;
    }
    bool ok = fwrite(hdr, 1, sizeof(hdr), f) == sizeof(hdr) &&
              fwrite(pcm, sizeof(int16_t), frames, f) == frames;
    if (ok) {
        fflush(f);
        /* LittleFS is power-loss safe; fsync also flushes the metadata. */
        (void)fsync(fileno(f));
    }
    fclose(f);

    if (!ok) {
        ESP_LOGE(TAG, "short write: %s", path);
        remove(path);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "wrote %s: %u frames (%" PRIu32 " bytes)",
             path, (unsigned)frames, data_bytes);
    return ESP_OK;
}

/* ---------------------------------------------------------------- reader */

static esp_err_t parse_wav(FILE *f, wav_info_t *info)
{
    uint8_t hdr[12];
    if (fread(hdr, 1, sizeof(hdr), f) != sizeof(hdr) ||
        memcmp(hdr, "RIFF", 4) != 0 || memcmp(hdr + 8, "WAVE", 4) != 0) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    bool have_fmt = false;
    uint16_t fmt_tag = 0;
    uint32_t byte_rate = 0;
    uint16_t block_align = 0;

    while (true) {
        uint8_t ch[8];
        if (fread(ch, 1, sizeof(ch), f) != sizeof(ch)) {
            break;
        }
        uint32_t size = rd32(ch + 4);
        long pos = ftell(f);
        if (pos < 0) {
            return ESP_FAIL;
        }

        if (memcmp(ch, "fmt ", 4) == 0 && size >= 16) {
            uint8_t fmt[16];
            if (fread(fmt, 1, sizeof(fmt), f) != sizeof(fmt)) {
                return ESP_ERR_INVALID_RESPONSE;
            }
            fmt_tag = rd16(fmt);
            info->channels = rd16(fmt + 2);
            info->sample_rate = rd32(fmt + 4);
            byte_rate = rd32(fmt + 8);
            block_align = rd16(fmt + 12);
            info->bits_per_sample = rd16(fmt + 14);
            have_fmt = true;
            long skip = (long)size - 16;
            if (skip > 0 && fseek(f, skip, SEEK_CUR) != 0) {
                return ESP_FAIL;
            }
        } else if (memcmp(ch, "data", 4) == 0) {
            if (!have_fmt) {
                return ESP_ERR_INVALID_RESPONSE;
            }
            info->data_offset = (uint32_t)pos;
            info->data_bytes = size;
            break;
        } else {
            if (fseek(f, (long)size, SEEK_CUR) != 0) {
                return ESP_FAIL;
            }
        }
        if (size & 1U) {
            if (fseek(f, 1, SEEK_CUR) != 0) {
                return ESP_FAIL;
            }
        }
    }

    if (!have_fmt || info->data_offset == 0 || fmt_tag != 1) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (info->channels != 1 && info->channels != 2) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (info->bits_per_sample != 16) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (byte_rate != info->sample_rate * info->channels * 2 ||
        block_align != info->channels * 2) {
        ESP_LOGW(TAG, "inconsistent WAV fmt: byte_rate=%" PRIu32 " align=%u",
                 byte_rate, (unsigned)block_align);
    }

    uint32_t frame_bytes = (uint32_t)info->channels * 2;
    info->frames = info->data_bytes / frame_bytes;
    info->duration_ms = (uint32_t)((uint64_t)info->frames * 1000 /
                                   (info->sample_rate ? info->sample_rate : 1));
    return ESP_OK;
}

esp_err_t audio_wav_info(const char *path, wav_info_t *info)
{
    if (path == NULL || info == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(info, 0, sizeof(*info));
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        return ESP_ERR_NOT_FOUND;
    }
    esp_err_t err = parse_wav(f, info);
    fclose(f);
    return err;
}

esp_err_t audio_wav_read(const char *path, int16_t **pcm, size_t *frames)
{
    if (path == NULL || pcm == NULL || frames == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *pcm = NULL;
    *frames = 0;

    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        return ESP_ERR_NOT_FOUND;
    }
    wav_info_t info;
    memset(&info, 0, sizeof(info));
    esp_err_t err = parse_wav(f, &info);
    if (err != ESP_OK) {
        fclose(f);
        return err;
    }
    if (info.sample_rate != AUDIO_SAMPLE_RATE) {
        ESP_LOGE(TAG, "%s: sample_rate=%" PRIu32 " (need %d)",
                 path, info.sample_rate, AUDIO_SAMPLE_RATE);
        fclose(f);
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (info.frames == 0) {
        fclose(f);
        return ESP_ERR_INVALID_SIZE;
    }

    int16_t *out = heap_caps_malloc((size_t)info.frames * sizeof(int16_t),
                                    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (out == NULL) {
        out = malloc((size_t)info.frames * sizeof(int16_t));
    }
    if (out == NULL) {
        fclose(f);
        return ESP_ERR_NO_MEM;
    }

    if (fseek(f, (long)info.data_offset, SEEK_SET) != 0) {
        free(out);
        fclose(f);
        return ESP_FAIL;
    }

    const size_t chunk_frames = 512;
    if (info.channels == 1) {
        size_t got = fread(out, sizeof(int16_t), info.frames, f);
        if (got != info.frames) {
            ESP_LOGE(TAG, "%s: short read %u/%" PRIu32,
                     path, (unsigned)got, info.frames);
            free(out);
            fclose(f);
            return ESP_FAIL;
        }
    } else {
        int16_t interleaved[chunk_frames * 2];
        size_t done = 0;
        while (done < info.frames) {
            size_t want = info.frames - done;
            if (want > chunk_frames) {
                want = chunk_frames;
            }
            size_t got = fread(interleaved, sizeof(int16_t), want * 2, f);
            size_t pairs = got / 2;
            for (size_t i = 0; i < pairs; i++) {
                int32_t mix = (int32_t)interleaved[i * 2] +
                              interleaved[i * 2 + 1];
                out[done + i] = (int16_t)(mix / 2);
            }
            done += pairs;
            if (pairs < want) {
                break;
            }
        }
        if (done != info.frames) {
            ESP_LOGE(TAG, "%s: stereo short read %u/%" PRIu32,
                     path, (unsigned)done, info.frames);
            free(out);
            fclose(f);
            return ESP_FAIL;
        }
    }

    fclose(f);
    *pcm = out;
    *frames = info.frames;
    return ESP_OK;
}

/* ----------------------------------------------------------------- paths */

esp_err_t audio_wav_make_rec_path(char *path, size_t path_len)
{
    if (path == NULL || path_len < 48) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = ensure_rec_dir();
    if (err != ESP_OK) {
        return err;
    }

    audio_wav_seed_clock();
    time_t now = time(NULL);
    struct timeval tv;
    gettimeofday(&tv, NULL);

    if (now >= 1700000000) {
        struct tm tm;
        localtime_r(&now, &tm);
        snprintf(path, path_len, "%s/rec_%04d%02d%02d_%02d%02d%02d_%03d.wav",
                 AUDIO_REC_DIR, tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                 tm.tm_hour, tm.tm_min, tm.tm_sec, (int)(tv.tv_usec / 1000));
    } else {
        snprintf(path, path_len, "%s/rec_boot_%05u_%03u.wav",
                 AUDIO_REC_DIR, (unsigned)(esp_timer_get_time() / 1000000),
                 (unsigned)((esp_timer_get_time() / 1000) % 1000));
    }
    return ESP_OK;
}

esp_err_t audio_wav_latest(char *path, size_t path_len)
{
    if (path == NULL || path_len < 32) {
        return ESP_ERR_INVALID_ARG;
    }
    DIR *dir = opendir(AUDIO_REC_DIR);
    if (dir == NULL) {
        return ESP_ERR_NOT_FOUND;
    }

    char best[160] = {0};
    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        if (strncmp(ent->d_name, "rec_", 4) != 0) {
            continue;
        }
        size_t n = strlen(ent->d_name);
        if (n < 5 || strcmp(ent->d_name + n - 4, ".wav") != 0) {
            continue;
        }
        if (best[0] == 0 || strcmp(ent->d_name, best) > 0) {
            snprintf(best, sizeof(best), "%.*s", (int)(sizeof(best) - 1),
                     ent->d_name);
        }
    }
    closedir(dir);

    if (best[0] == 0) {
        return ESP_ERR_NOT_FOUND;
    }
    snprintf(path, path_len, "%s/%s", AUDIO_REC_DIR, best);
    return ESP_OK;
}

const char *audio_wav_duration_str(uint32_t frames, char *buf, size_t buf_len)
{
    if (buf == NULL || buf_len == 0) {
        return "";
    }
    uint32_t ms = (uint32_t)((uint64_t)frames * 1000 / AUDIO_SAMPLE_RATE);
    snprintf(buf, buf_len, "%u.%u s", (unsigned)(ms / 1000),
             (unsigned)((ms % 1000) / 100));
    return buf;
}
