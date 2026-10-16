/*
 * Copyright (c) 2025 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <zmk/split/transport/peripheral.h>
#include <zmk/split/transport/types.h>

int zmk_split_peripheral_report_event(const struct zmk_split_transport_peripheral_event *event);

const struct zmk_split_transport_peripheral *zmk_split_peripheral_get_active_transport(void);

#if IS_ENABLED(CONFIG_ZMK_SPLIT_WIRED)

bool zmk_split_wired_is_selected(void);

#endif // IS_ENABLED(CONFIG_ZMK_SPLIT_WIRED)
