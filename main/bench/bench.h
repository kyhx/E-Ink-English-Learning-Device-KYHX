/*
 * bench.h — UART console for hardware bring-up / measurements.
 *
 * Commands are intentionally one level deep so they are easy to type on a
 * serial terminal:
 *
 *   bench info
 *   bench fs
 *   bench display band <y0> <h> <n>
 *   bench display full
 *   bench display clean
 *   bench keys [seconds]
 *   bench selftest
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef BENCH_H
#define BENCH_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Start the UART0 REPL (115200 8N1) and register commands. */
esp_err_t bench_init(void);

#ifdef __cplusplus
}
#endif
#endif /* BENCH_H */
