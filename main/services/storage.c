/*
 * storage.c — LittleFS mount + 64 KB read/write self-test.
 *
 * SPDX-License-Identifier: MIT
 */
#include "storage.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_littlefs.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "esp_timer.h"

static const char *TAG = "storage";
static bool s_mounted;

esp_err_t storage_init(void)
{
    const esp_partition_t *part = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_LITTLEFS,
        STORAGE_PARTITION_LABEL);
    if (part == NULL) {
        ESP_LOGE(TAG, "partition '%s' not found; check partitions.csv",
                 STORAGE_PARTITION_LABEL);
        return ESP_ERR_NOT_FOUND;
    }
    ESP_LOGI(TAG, "partition '%s': offset=0x%08" PRIx32 ", size=%" PRIu32 " KB",
             STORAGE_PARTITION_LABEL, part->address, part->size / 1024);

    esp_vfs_littlefs_conf_t conf = {
        .base_path = STORAGE_MOUNT_POINT,
        .partition_label = STORAGE_PARTITION_LABEL,
        .format_if_mount_failed = true,
        .read_only = false,
        .dont_mount = false,
        .grow_on_mount = true,
    };

    esp_err_t err = esp_vfs_littlefs_register(&conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mount failed: %s", esp_err_to_name(err));
        s_mounted = false;
        return err;
    }

    s_mounted = true;
    ESP_LOGI(TAG, "LittleFS mounted at %s", STORAGE_MOUNT_POINT);
    storage_info();
    return ESP_OK;
}

bool storage_is_mounted(void)
{
    return s_mounted;
}

esp_err_t storage_info(void)
{
    if (!s_mounted) {
        ESP_LOGW(TAG, "not mounted");
        return ESP_ERR_INVALID_STATE;
    }

    size_t total = 0;
    size_t used = 0;
    esp_err_t err = esp_littlefs_info(STORAGE_PARTITION_LABEL, &total, &used);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_littlefs_info failed: %s", esp_err_to_name(err));
        return err;
    }

    printf("LittleFS: total=%u KB used=%u KB free=%u KB\n",
           (unsigned)(total / 1024), (unsigned)(used / 1024),
           (unsigned)((total - used) / 1024));
    return ESP_OK;
}

size_t storage_free_bytes(void)
{
    size_t total = 0;
    size_t used = 0;
    if (!s_mounted ||
        esp_littlefs_info(STORAGE_PARTITION_LABEL, &total, &used) != ESP_OK) {
        return 0;
    }
    return total > used ? total - used : 0;
}

esp_err_t storage_selftest(void)
{
    if (!s_mounted) {
        ESP_LOGW(TAG, "not mounted");
        return ESP_ERR_INVALID_STATE;
    }

    const size_t size = 64 * 1024;
    uint8_t *wbuf = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (wbuf == NULL) {
        wbuf = malloc(size);
    }
    uint8_t *rbuf = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (rbuf == NULL) {
        rbuf = malloc(size);
    }
    if (wbuf == NULL || rbuf == NULL) {
        free(wbuf);
        free(rbuf);
        ESP_LOGE(TAG, "out of memory for 64 KB buffers");
        return ESP_ERR_NO_MEM;
    }

    for (size_t i = 0; i < size; i++) {
        wbuf[i] = (uint8_t)((i * 31u) ^ (i >> 8));
    }

    const char *path = STORAGE_MOUNT_POINT "/selftest.bin";
    int64_t t0 = esp_timer_get_time();

    FILE *f = fopen(path, "wb");
    if (f == NULL) {
        ESP_LOGE(TAG, "fopen(%s) for write failed", path);
        free(wbuf);
        free(rbuf);
        return ESP_FAIL;
    }
    size_t written = fwrite(wbuf, 1, size, f);
    fclose(f);
    if (written != size) {
        ESP_LOGE(TAG, "short write: %u/%u", (unsigned)written, (unsigned)size);
        free(wbuf);
        free(rbuf);
        return ESP_FAIL;
    }
    int64_t t1 = esp_timer_get_time();

    f = fopen(path, "rb");
    if (f == NULL) {
        ESP_LOGE(TAG, "fopen(%s) for read failed", path);
        free(wbuf);
        free(rbuf);
        return ESP_FAIL;
    }
    size_t rd = fread(rbuf, 1, size, f);
    fclose(f);
    int64_t t2 = esp_timer_get_time();

    bool ok = (rd == size) && (memcmp(wbuf, rbuf, size) == 0);
    ESP_LOGI(TAG, "64 KB write=%lld ms read=%lld ms verify=%s",
             (long long)((t1 - t0) / 1000), (long long)((t2 - t1) / 1000),
             ok ? "OK" : "FAIL");

    remove(path);
    free(wbuf);
    free(rbuf);
    return ok ? ESP_OK : ESP_FAIL;
}
