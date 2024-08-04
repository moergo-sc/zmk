/*
 * Copyright (c) 2020 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <zmk/event_manager.h>
#include <zmk/point_device.h>

struct zmk_pd_raw_event {
  enum pd_type type;
  uint8_t id;
  int16_t dx;
  int16_t dy;
  int dt; // in ms
  int64_t update_time; // in ms
};

ZMK_EVENT_DECLARE(zmk_pd_raw_event);
