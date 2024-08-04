/*
 * Copyright (c) 2020 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#define ZMK_KEYMAP_SLIDERS_NODE DT_INST(0, zmk_keymap_sliders)
#define ZMK_KEYMAP_HAS_SLIDERS DT_NODE_HAS_STATUS(ZMK_KEYMAP_SLIDERS_NODE, okay)
#define ZMK_KEYMAP_SLIDERS_LEN DT_PROP_LEN(ZMK_KEYMAP_SLIDERS_NODE, sliders)
#define ZMK_KEYMAP_SLIDERS_BY_IDX(idx) DT_PHANDLE_BY_IDX(ZMK_KEYMAP_SLIDERS_NODE, sliders, idx)
