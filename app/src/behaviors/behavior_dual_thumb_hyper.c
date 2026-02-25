/*
 * Copyright (c) 2025 Seth Voltz
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_behavior_dual_thumb_hyper

#include <zephyr/device.h>
#include <drivers/behavior.h>
#include <zephyr/logging/log.h>

#include <zmk/keymap.h>
#include <zmk/behavior.h>
#include <zmk/hid.h>
#include <zmk/endpoints.h>
#include <dt-bindings/zmk/modifiers.h>
#include <dt-bindings/zmk/hid_usage_pages.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define HYPER_MODS (MOD_LGUI | MOD_LALT | MOD_LCTL | MOD_LSFT)

static bool left_down, right_down, hyper_active;
static uint8_t left_layer, right_layer;

static void recompute(void) {
    if (left_down && right_down) {
        zmk_keymap_layer_deactivate(left_layer);
        zmk_keymap_layer_deactivate(right_layer);
        if (!hyper_active) {
            zmk_hid_register_mods(HYPER_MODS);
            hyper_active = true;
            zmk_endpoints_send_report(HID_USAGE_KEY);
        }
    } else {
        if (hyper_active) {
            zmk_hid_unregister_mods(HYPER_MODS);
            hyper_active = false;
            zmk_endpoints_send_report(HID_USAGE_KEY);
        }
        if (left_down) {
            zmk_keymap_layer_activate(left_layer);
        } else {
            zmk_keymap_layer_deactivate(left_layer);
        }
        if (right_down) {
            zmk_keymap_layer_activate(right_layer);
        } else {
            zmk_keymap_layer_deactivate(right_layer);
        }
    }
}

static int dth_keymap_binding_pressed(struct zmk_behavior_binding *binding,
                                      struct zmk_behavior_binding_event event) {
    LOG_DBG("position %d side %d layer %d", event.position, binding->param1, binding->param2);

    if (binding->param1 == 0) {
        left_down = true;
        left_layer = binding->param2;
    } else {
        right_down = true;
        right_layer = binding->param2;
    }

    recompute();
    return ZMK_BEHAVIOR_OPAQUE;
}

static int dth_keymap_binding_released(struct zmk_behavior_binding *binding,
                                       struct zmk_behavior_binding_event event) {
    LOG_DBG("position %d side %d layer %d", event.position, binding->param1, binding->param2);

    if (binding->param1 == 0) {
        left_down = false;
    } else {
        right_down = false;
    }

    recompute();
    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api behavior_dual_thumb_hyper_driver_api = {
    .binding_pressed = dth_keymap_binding_pressed,
    .binding_released = dth_keymap_binding_released,
};

BEHAVIOR_DT_INST_DEFINE(0, NULL, NULL, NULL, NULL, POST_KERNEL,
                        CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,
                        &behavior_dual_thumb_hyper_driver_api);
