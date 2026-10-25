/*
 * buttons.c — polling debounce + short/long press events.
 *
 * SPDX-License-Identifier: MIT
 */
#include "buttons.h"
#include "board_pins.h"

#include <string.h>

#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "buttons";

static const gpio_num_t s_gpios[BOARD_KEY_COUNT] = {
    BOARD_KEY_PREV_GPIO,
    BOARD_KEY_NEXT_GPIO,
    BOARD_KEY_PLAY_GPIO,
    BOARD_KEY_RECORD_GPIO,
    BOARD_KEY_SYNC_GPIO,
};

static const char *const s_key_names[BOARD_KEY_COUNT] = {
    "PREV", "NEXT", "PLAY", "RECORD", "SYNC",
};

/* Explicit maps: do not rely on enum arithmetic. */
static const button_event_t s_short_events[BOARD_KEY_COUNT] = {
    BUTTON_EVENT_PREV_SHORT,
    BUTTON_EVENT_NEXT_SHORT,
    BUTTON_EVENT_PLAY_SHORT,
    BUTTON_EVENT_RECORD_SHORT,
    BUTTON_EVENT_SYNC_SHORT,
};

static const button_event_t s_long_events[BOARD_KEY_COUNT] = {
    BUTTON_EVENT_PREV_LONG,
    BUTTON_EVENT_NEXT_LONG,
    BUTTON_EVENT_PLAY_LONG,
    BUTTON_EVENT_RECORD_LONG,
    BUTTON_EVENT_SYNC_LONG,
};

typedef struct {
    bool stable;          /* debounced state, true = pressed */
    bool raw;             /* last raw sample */
    bool long_sent;       /* long event already emitted for this press */
    uint32_t last_change_ms;
    uint32_t press_ms;
} key_state_t;

static key_state_t s_keys[BOARD_KEY_COUNT];
static bool s_inited;

/* Small event queue: a poll can see several keys change at once. */
#define EVENT_QUEUE_LEN 16
static button_event_t s_queue[EVENT_QUEUE_LEN];
static int s_q_head;
static int s_q_tail;
static int s_q_count;

static bool read_pressed(int idx)
{
    return gpio_get_level(s_gpios[idx]) == 0;
}

static void push_event(button_event_t ev)
{
    if (s_q_count >= EVENT_QUEUE_LEN) {
        /* Drop the oldest event rather than losing the newest one. */
        s_q_head = (s_q_head + 1) % EVENT_QUEUE_LEN;
        s_q_count--;
        ESP_LOGW(TAG, "event queue full, dropping oldest");
    }
    s_queue[s_q_tail] = ev;
    s_q_tail = (s_q_tail + 1) % EVENT_QUEUE_LEN;
    s_q_count++;
}

static button_event_t pop_event(void)
{
    if (s_q_count == 0) {
        return BUTTON_EVENT_NONE;
    }
    button_event_t ev = s_queue[s_q_head];
    s_q_head = (s_q_head + 1) % EVENT_QUEUE_LEN;
    s_q_count--;
    return ev;
}

esp_err_t buttons_init(void)
{
    uint64_t mask = 0;
    for (int i = 0; i < BOARD_KEY_COUNT; i++) {
        mask |= (1ULL << s_gpios[i]);
    }

    gpio_config_t io = {
        .pin_bit_mask = mask,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&io);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "gpio_config failed: %s", esp_err_to_name(err));
        return err;
    }

    memset(s_keys, 0, sizeof(s_keys));
    for (int i = 0; i < BOARD_KEY_COUNT; i++) {
        bool down = read_pressed(i);
        s_keys[i].stable = down;
        s_keys[i].raw = down;
        /* If a key is held at boot, suppress the long event until release. */
        s_keys[i].long_sent = down;
        s_keys[i].last_change_ms = 0;
        s_keys[i].press_ms = 0;
    }

    s_q_head = s_q_tail = s_q_count = 0;
    s_inited = true;

    ESP_LOGI(TAG, "5 keys ready: PREV=GPIO%d NEXT=GPIO%d PLAY=GPIO%d RECORD=GPIO%d SYNC=GPIO%d",
             s_gpios[0], s_gpios[1], s_gpios[2], s_gpios[3], s_gpios[4]);
    return ESP_OK;
}

button_event_t buttons_poll(uint32_t now_ms)
{
    if (!s_inited) {
        return BUTTON_EVENT_NONE;
    }

    for (int i = 0; i < BOARD_KEY_COUNT; i++) {
        key_state_t *k = &s_keys[i];
        bool raw = read_pressed(i);

        if (raw != k->raw) {
            k->raw = raw;
            k->last_change_ms = now_ms;
        }

        /* Debounce: only accept a new level after it has been stable. */
        if (k->stable != k->raw &&
            (uint32_t)(now_ms - k->last_change_ms) >= BUTTON_DEBOUNCE_MS) {
            k->stable = k->raw;
            if (k->stable) {
                k->press_ms = now_ms;
                k->long_sent = false;
            } else {
                if (!k->long_sent) {
                    push_event(s_short_events[i]);
                }
                k->long_sent = true;
            }
        }

        /* Long press fires while still held. */
        if (k->stable && !k->long_sent &&
            (uint32_t)(now_ms - k->press_ms) >= BUTTON_LONG_PRESS_MS) {
            k->long_sent = true;
            push_event(s_long_events[i]);
        }
    }

    return pop_event();
}

bool buttons_is_down(board_key_t key)
{
    if (!s_inited || key >= BOARD_KEY_COUNT) {
        return false;
    }
    return s_keys[key].stable;
}

uint8_t buttons_mask(void)
{
    uint8_t mask = 0;
    if (!s_inited) {
        return 0;
    }
    for (int i = 0; i < BOARD_KEY_COUNT; i++) {
        if (s_keys[i].stable) {
            mask |= (uint8_t)(1u << i);
        }
    }
    return mask;
}

const char *buttons_key_name(board_key_t key)
{
    if (key >= BOARD_KEY_COUNT) {
        return "?";
    }
    return s_key_names[key];
}

const char *buttons_event_name(button_event_t event)
{
    switch (event) {
    case BUTTON_EVENT_PREV_SHORT:   return "PREV short";
    case BUTTON_EVENT_PREV_LONG:    return "PREV long";
    case BUTTON_EVENT_NEXT_SHORT:   return "NEXT short";
    case BUTTON_EVENT_NEXT_LONG:    return "NEXT long";
    case BUTTON_EVENT_PLAY_SHORT:   return "PLAY short";
    case BUTTON_EVENT_PLAY_LONG:    return "PLAY long";
    case BUTTON_EVENT_RECORD_SHORT: return "RECORD short";
    case BUTTON_EVENT_RECORD_LONG:  return "RECORD long";
    case BUTTON_EVENT_SYNC_SHORT:   return "SYNC short";
    case BUTTON_EVENT_SYNC_LONG:    return "SYNC long";
    default:                        return "NONE";
    }
}
