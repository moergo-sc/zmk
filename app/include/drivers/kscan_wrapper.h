/*
 * Copyright (c) 2020 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <drivers/kscan.h>

#ifdef __cplusplus
extern "C" {
#endif

  typedef void (*kscan_slider_callback_t)(const struct device *dev,
                                          int16_t dPos, int dT);
  typedef void (*kscan_slider_config_t)(const struct device *dev,
                                        kscan_slider_callback_t callback,
                                        int id);

  struct kscan_slider_api {
    struct kscan_driver_api kscan_api;
    kscan_slider_config_t   slider_config;
  };

  void kscan_slider_config(const struct device *dev,
                           kscan_slider_callback_t callback,
                           int id);

#ifdef __cplusplus
}
#endif
