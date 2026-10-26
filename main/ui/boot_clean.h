/*
 * boot_clean.h — cold-boot e-paper cleaning shared by app and collect modes.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef BOOT_CLEAN_H
#define BOOT_CLEAN_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* POWERON / EXT / BROWNOUT need a clean; esp_restart() should skip it. */
bool boot_clean_should_run(void);

/* Run white -> black -> base -> partial LUT reload.
 * `base` must already contain the page background; `frame` is scratch. */
bool boot_clean_run(uint8_t *base, uint8_t *frame, const char *tag);

#ifdef __cplusplus
}
#endif
#endif /* BOOT_CLEAN_H */
