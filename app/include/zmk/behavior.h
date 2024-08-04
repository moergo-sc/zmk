/*
 * Copyright (c) 2020 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <zephyr/device.h>

#define ZMK_BEHAVIOR_OPAQUE 0
#define ZMK_BEHAVIOR_TRANSPARENT 1

struct zmk_behavior_binding {
    char *behavior_dev;
    uint32_t param1;
    uint32_t param2;
    uint32_t param3;
    uint32_t param4;
};

struct zmk_behavior_binding_event {
    int layer;
    uint32_t position;
    int64_t timestamp;
};
<<<<<<< HEAD

/**
 * @brief Get a const struct device* for a behavior from its @p name field.
 *
 * @param name Behavior name to search for.
 *
 * @retval Pointer to the device structure for the behavior with the given name.
 * @retval NULL if the behavior is not found or its initialization function failed.
 *
 * @note This is equivalent to device_get_binding(), except it only searches
 * behavior devices, so it is faster and there is no chance of it returning an
 * unrelated node which shares the same name as a behavior.
 */
const struct device *zmk_behavior_get_binding(const char *name);
=======
>>>>>>> 5591ade36fef72969c7328b61dd0da901d713048
