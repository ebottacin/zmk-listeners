/*
 * Copyright (c) 2025 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_hid_listeners

#include <zephyr/device.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/dlist.h>
#include <zephyr/kernel.h>
#include <drivers/behavior.h>
#include <zmk/behavior.h>
#include <zmk/behavior_queue.h>
#include <zmk/event_manager.h>
#include <zmk/events/hid_indicators_changed.h>
#include <zmk/keys.h>
#include <zmk/keymap.h>
#include <dt-bindings/zmk/hid_usage.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

struct hid_listener_cfg {
    uint8_t indicator;
    size_t bindings_len;
    struct zmk_behavior_binding *bindings;
};

#define TRANSFORMED_BINDINGS(n)                                                                    \
    { LISTIFY(DT_PROP_LEN(n, bindings), ZMK_KEYMAP_EXTRACT_BINDING, (, ), n) }

#define HID_LISTENER_INST(n)                                                                       \
    static struct zmk_behavior_binding                                                             \
        hid_listener_config_##n##_bindings[DT_PROP_LEN(n, bindings)] = TRANSFORMED_BINDINGS(n);    \
                                                                                                   \
    static struct hid_listener_cfg hid_listener_cfg_##n = {                                        \
        .bindings_len = DT_PROP_LEN(n, bindings),                                                  \
        .bindings = hid_listener_config_##n##_bindings,                                            \
        .indicator = DT_PROP(n, indicator),                                                        \
    };

DT_INST_FOREACH_CHILD(0, HID_LISTENER_INST)

#define HID_LISTENER_ITEM(n) &hid_listener_cfg_##n,
#define HID_LISTENER_UTIL_ONE(n) 1 +

static struct hid_listener_cfg *listeners[] = {DT_INST_FOREACH_CHILD(0, HID_LISTENER_ITEM)};

#define LISTENERS_LEN (DT_INST_FOREACH_CHILD(0, HID_LISTENER_UTIL_ONE) 0)

#define TAP_MS DT_INST_PROP(0, tap_ms)
#define WAIT_MS DT_INST_PROP(0, wait_ms)

// todo: are these defined anywhere already?
#define FLAG_NUM_LOCK (1 << 0)
#define FLAG_CAPS_LOCK (1 << 1)
#define FLAG_SCROLL_LOCK (1 << 2)
#define FLAG_COMPOSE (1 << 3)
#define FLAG_KANA (1 << 4)

unsigned int get_hid_flag(uint8_t code) {
    switch (code) {
    case HID_USAGE_LED_NUM_LOCK:
        return FLAG_NUM_LOCK;
    case HID_USAGE_LED_CAPS_LOCK:
        return FLAG_CAPS_LOCK;
    case HID_USAGE_LED_SCROLL_LOCK:
        return FLAG_SCROLL_LOCK;
    case HID_USAGE_LED_COMPOSE:
        return FLAG_COMPOSE;
    case HID_USAGE_LED_KANA:
        return FLAG_KANA;
    default:
        return 0;
    }
}

static int last_indicators = 0;

static int hid_state_listener(const zmk_event_t *eh) {
    struct zmk_hid_indicators_changed *ev = as_zmk_hid_indicators_changed(eh);

    for (int i = 0; i < LISTENERS_LEN; i++) {
        const struct hid_listener_cfg *cfg = listeners[i];
        const unsigned int flag = get_hid_flag(cfg->indicator);

        struct zmk_behavior_binding_event event = {
            .position = INT32_MAX,
            .timestamp = k_uptime_get(),
#if IS_ENABLED(CONFIG_ZMK_SPLIT)
            .source = ZMK_POSITION_STATE_CHANGE_SOURCE_LOCAL,
#endif
        };

        LOG_DBG("checking indicators %d, with flag=%d", ev->indicators, flag);
        if ((ev->indicators & flag) != 0 && (last_indicators & flag) == 0) {
            LOG_DBG("invoking hid listener on behavior: %d, indicator=%d", i, cfg->indicator);
            zmk_behavior_queue_add(&event, cfg->bindings[0], true, TAP_MS);
            zmk_behavior_queue_add(&event, cfg->bindings[0], false, WAIT_MS);
        } else if (cfg->bindings_len > 1 && (ev->indicators & flag) == 0 &&
                   (last_indicators & flag) != 0) {
            LOG_DBG("invoking hid listener off behavior: %d, indicator=%d", i, cfg->indicator);
            zmk_behavior_queue_add(&event, cfg->bindings[1], true, TAP_MS);
            zmk_behavior_queue_add(&event, cfg->bindings[1], false, WAIT_MS);
        }
    }

    last_indicators = ev->indicators;

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(hid_listeners, hid_state_listener);
ZMK_SUBSCRIPTION(hid_listeners, zmk_hid_indicators_changed);