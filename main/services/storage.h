/*
 * storage.h — LittleFS mount + self-test.
 *
 * The "storage" partition is declared in partitions.csv (10 MB, subtype
 * littlefs).  All persistent app data (word bank, audio pack, records,
 * collection WAVs) will live under /storage.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef STORAGE_H
#define STORAGE_H

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define STORAGE_MOUNT_POINT "/storage"
#define STORAGE_PARTITION_LABEL "storage"

/* Mount LittleFS, formatting the partition on first use. */
esp_err_t storage_init(void);

bool storage_is_mounted(void);

/* Print total/used/free bytes. */
esp_err_t storage_info(void);

/* Write a 64 KB pattern file, read it back, compare, delete. */
esp_err_t storage_selftest(void);

/* Free bytes, or 0 if not mounted / not available. */
size_t storage_free_bytes(void);

#ifdef __cplusplus
}
#endif
#endif /* STORAGE_H */
