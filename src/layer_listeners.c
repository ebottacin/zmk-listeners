/*
 * Copyright (c) 2025 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_layer_listeners

#include <zephyr/device.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/dlist.h>
#include <zephyr/kernel.h>
#include <drivers/behavior.h>
#include <zmk/behavior.h>
#include <zmk/behavior_queue.h>
#include <zmk/event_manager.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/keys.h>
#include <zmk/keymap.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

struct layer_listener_cfg {
    size_t bindings_len;
    struct zmk_behavior_binding *bindings;
    uint8_t layers[ZMK_KEYMAP_LAYERS_LEN];
    size_t layers_len;
};

#define TRANSFORMED_BINDINGS(n)                                                                    \
    { LISTIFY(DT_PROP_LEN(n, bindings), ZMK_KEYMAP_EXTRACT_BINDING, (, ), n) }

#define LAYER_LISTENER_INST(n)                                                                     \
    static struct zmk_behavior_binding                                                             \
        layer_listener_config_##n##_bindings[DT_PROP_LEN(n, bindings)] = TRANSFORMED_BINDINGS(n);  \
                                                                                                   \
    static struct layer_listener_cfg layer_listener_cfg_##n = {                                    \
        .bindings_len = DT_PROP_LEN(n, bindings),                                                  \
        .bindings = layer_listener_config_##n##_bindings,                                          \
        .layers = DT_PROP(n, layers),                                                              \
        .layers_len = DT_PROP_LEN(n, layers),                                                      \
    };

DT_INST_FOREACH_CHILD(0, LAYER_LISTENER_INST)

#define LAYER_LISTENER_ITEM(n) &layer_listener_cfg_##n,
#define LAYER_LISTENER_UTIL_ONE(n) 1 +

static struct layer_listener_cfg *listeners[] = {DT_INST_FOREACH_CHILD(0, LAYER_LISTENER_ITEM)};

#define LISTENERS_LEN (DT_INST_FOREACH_CHILD(0, LAYER_LISTENER_UTIL_ONE) 0)

#define TAP_MS DT_INST_PROP(0, tap_ms)
#define WAIT_MS DT_INST_PROP(0, wait_ms)

/* Tracks the layer whose entry binding last ran, to avoid re-queuing it twice
 * when an exit event and an entry event resolve to the same layer. */
static int last_entered_layer = -1;

static const struct layer_listener_cfg *find_listener_for_layer(uint8_t layer) {
    for (int i = 0; i < LISTENERS_LEN; i++) {
        const struct layer_listener_cfg *cfg = listeners[i];

        for (int j = 0; j < cfg->layers_len; j++) {
            if (cfg->layers[j] == layer) {
                return cfg;
            }
        }
    }

    return NULL;
}

static void queue_binding(struct zmk_behavior_binding_event event,
                          struct zmk_behavior_binding binding) {
    zmk_behavior_queue_add(&event, binding, true, TAP_MS);
    zmk_behavior_queue_add(&event, binding, false, WAIT_MS);
}

static int layer_state_listener(const zmk_event_t *eh) {
    struct zmk_layer_state_changed *ev = as_zmk_layer_state_changed(eh);

    for (int i = 0; i < LISTENERS_LEN; i++) {
        const struct layer_listener_cfg *cfg = listeners[i];

        for (int j = 0; j < cfg->layers_len; j++) {
            if (ev->layer == cfg->layers[j]) {
                LOG_DBG("invoking listener %d, layer=%d state=%d", i, ev->layer, ev->state);

                struct zmk_behavior_binding_event event = {
                    .position = INT32_MAX,
                    .timestamp = k_uptime_get(),
#if IS_ENABLED(CONFIG_ZMK_SPLIT)
                    .source = ZMK_POSITION_STATE_CHANGE_SOURCE_LOCAL,
#endif
                };

                if (ev->state) {
                    queue_binding(event, cfg->bindings[0]);
                    last_entered_layer = ev->layer;
                } else {
                    /* Re-resolve the layer we fall back to instead of blindly
                     * running this layer's exit binding, so stacked layers
                     * restore the still-active layer's entry action. */
                    uint8_t resolved_layer = zmk_keymap_highest_layer_active();
                    const struct layer_listener_cfg *entry_cfg =
                        find_listener_for_layer(resolved_layer);

                    if (entry_cfg != NULL) {
                        if (resolved_layer != last_entered_layer) {
                            queue_binding(event, entry_cfg->bindings[0]);
                            last_entered_layer = resolved_layer;
                        }
                    } else if (cfg->bindings_len > 1) {
                        queue_binding(event, cfg->bindings[1]);
                    }
                }

                break;
            }
        }
    }

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(layer_listeners, layer_state_listener);
ZMK_SUBSCRIPTION(layer_listeners, zmk_layer_state_changed);
