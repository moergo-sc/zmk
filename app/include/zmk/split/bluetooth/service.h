/*
 * Copyright (c) 2020 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

<<<<<<< HEAD
#include <zmk/events/sensor_event.h>
#include <zmk/sensors.h>
=======
#include <drivers/sensor.h>
>>>>>>> 5591ade36fef72969c7328b61dd0da901d713048

#define ZMK_SPLIT_RUN_BEHAVIOR_DEV_LEN 9

struct sensor_event {
    uint8_t sensor_index;

    uint8_t channel_data_size;
    struct zmk_sensor_channel_data channel_data[ZMK_SENSOR_EVENT_MAX_CHANNELS];
} __packed;

struct zmk_split_run_behavior_data {
    uint8_t position;
    uint8_t state;
    uint32_t param1;
    uint32_t param2;
} __packed;

struct zmk_split_run_behavior_payload {
    struct zmk_split_run_behavior_data data;
    char behavior_dev[ZMK_SPLIT_RUN_BEHAVIOR_DEV_LEN];
} __packed;


int zmk_split_bt_position_pressed(uint8_t position);
int zmk_split_bt_position_released(uint8_t position);
<<<<<<< HEAD
int zmk_split_bt_sensor_triggered(uint8_t sensor_index,
                                  const struct zmk_sensor_channel_data channel_data[],
                                  size_t channel_data_size);
=======
int zmk_split_bt_sensor_triggered(uint8_t sensor_number, struct sensor_value value);
>>>>>>> 5591ade36fef72969c7328b61dd0da901d713048
