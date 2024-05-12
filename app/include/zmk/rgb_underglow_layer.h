/*
 * Copyright (c) 2020 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once
#include <zmk/keymap.h>

#define ZMK_RGB_CHILD_LEN_PLUS_ONE(node) 1 +

#define ZMK_RGBMAP_LAYERS_LEN                                                                      \
    (DT_FOREACH_CHILD(DT_INST(0, zmk_underglow_layer), ZMK_RGB_CHILD_LEN_PLUS_ONE) 0)

const int zmk_rgbmap_id(uint8_t layer);
uint32_t *rgb_underglow_get_bindings(uint8_t layer);
uint8_t rgb_underglow_top_layer_with_state(uint32_t state_to_test);
uint8_t rgb_underglow_top_layer(void);