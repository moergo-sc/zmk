/*
 * Copyright (c) 2020 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * @file slider.h
 *
 * @brief Common header file for all devices supporting slide manueover
 */

#pragma once

#include <drivers/kscan_wrapper.h>

#ifdef __cplusplus
extern "C" {
#endif
  struct slider_data {
    kscan_slider_callback_t callback; // callback to process when slider mode is enabled
    uint8_t     id; // needed when multiple sliders exist to get the corresponding behavior
    uint8_t step;
    int16_t delta_position;
    int16_t acc_position;
    int64_t pre_ts;
    int     delta_time;
  };

#ifdef __cplusplus
}

#endif
