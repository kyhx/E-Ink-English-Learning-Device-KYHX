/*
 * buttons.h — 5-key polling driver with 20 ms debounce and long press.
 *
 * All keys are active-low with internal pull-ups.  `buttons_poll()` is meant
 * to be called from the app loop every 5–10 ms; it never blocks.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef BUTTONS_H
#define BUTTONS_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BOARD_KEY_PREV = 0,
    BOARD_KEY_NEXT,
    BOARD_KEY_PLAY,
    BOARD_KEY_RECORD,
    BOARD_KEY_SYNC,
    BOARD_KEY_COUNT
} board_key_t;

typedef enum {
    BUTTON_EVENT_NONE = 0,
    BUTTON_EVENT_PREV_SHORT,
    BUTTON_EVENT_PREV_LONG,
    BUTTON_EVENT_NEXT_SHORT,
    BUTTON_EVENT_NEXT_LONG,
    BUTTON_EVENT_PLAY_SHORT,
    BUTTON_EVENT_PLAY_LONG,
    BUTTON_EVENT_RECORD_SHORT,
    BUTTON_EVENT_RECORD_LONG,
    BUTTON_EVENT_SYNC_SHORT,
    BUTTON_EVENT_SYNC_LONG,
} button_event_t;

#define BUTTON_DEBOUNCE_MS  20
#define BUTTON_LONG_PRESS_MS 800

/* Configure the five key GPIOs as inputs with pull-ups. */
esp_err_t buttons_init(void);

/* Call every 5–10 ms with a monotonic millisecond timestamp.
 * Returns at most one queued event per call. */
button_event_t buttons_poll(uint32_t now_ms);

/* Raw debounced state: true = pressed. */
bool buttons_is_down(board_key_t key);

/* 5-bit mask of debounced pressed keys, bit 0 = PREV ... bit 4 = SYNC. */
uint8_t buttons_mask(void);

const char *buttons_key_name(board_key_t key);
const char *buttons_event_name(button_event_t event);

#ifdef __cplusplus
}
#endif
#endif /* BUTTONS_H */
