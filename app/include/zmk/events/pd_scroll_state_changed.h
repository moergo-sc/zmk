/*
 * Copyright (c) 2020 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <zmk/event_manager.h>

struct zmk_pd_scroll_state_changed {
  int16_t x;
  int16_t y;
};

ZMK_EVENT_DECLARE(zmk_pd_scroll_state_changed);
