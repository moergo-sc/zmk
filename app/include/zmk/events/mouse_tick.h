
/*
 * Copyright (c) 2020 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <dt-bindings/zmk/mouse.h>
#include <zephyr/kernel.h>
#include <zmk/event_manager.h>
#include <zmk/mouse.h>

struct zmk_mouse_tick {
    struct vector2d max_move;
    struct vector2d max_scroll;
    struct mouse_config move_config;
    struct mouse_config scroll_config;
    struct mouse_times start_times;
    int64_t timestamp;
};

ZMK_EVENT_DECLARE(zmk_mouse_tick);
