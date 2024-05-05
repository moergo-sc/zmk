/*
 * Copyright (c) 2020 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/device.h>
#include <drivers/behavior.h>
#include <zephyr/sys/util.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <zmk/matrix.h>
#include <zmk/keymap.h>
#include <zmk/rgb_underglow_layer.h>

#if !IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
#include <zmk/split/bluetooth/peripheral_layers.h>
#endif

#define DT_DRV_COMPAT zmk_underglow_layer

#define LAYER_ID(node) DT_PROP(node, layer_id)
#define RGB_BINDINGS(node) DT_PROP(node, bindings)

static uint32_t zmk_rgbmap[ZMK_RGBMAP_LAYERS_LEN][ZMK_KEYMAP_LEN] = {
    DT_INST_FOREACH_CHILD_SEP(0, RGB_BINDINGS, (, ))
};

static int zmk_rgbmap_ids[ZMK_RGBMAP_LAYERS_LEN] = {
    DT_INST_FOREACH_CHILD_SEP(0, LAYER_ID, (, ))};


const int zmk_rgbmap_id(uint8_t layer) {
    for (uint8_t i = 0; i < ZMK_RGBMAP_LAYERS_LEN; i++) {
        if (zmk_rgbmap_ids[i] == layer) {
            return i;
        }
    }
    return -1;
}

uint32_t *rgb_underglow_get_bindings(void) {
    uint8_t layer = rgb_underglow_top_layer();
    int rgblayer = zmk_rgbmap_id(layer);
    if (rgblayer == -1){
        return NULL;
    } else {
        return zmk_rgbmap[rgblayer];
    }
}

uint8_t rgb_underglow_top_layer(void) {
    for (uint8_t layer = ZMK_KEYMAP_LAYERS_LEN - 1; layer > 0; layer--) {
#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
        if (zmk_keymap_layer_active(layer)) {
#else
        if (peripheral_layer_active(layer)) {
#endif
            return layer;
        }
    }
    return -1;
}
