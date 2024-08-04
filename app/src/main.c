/*
 * Copyright (c) 2020 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/settings/settings.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <zmk/matrix.h>
#include <zmk/kscan.h>
#include <zmk/display.h>
#include <drivers/ext_power.h>

<<<<<<< HEAD
int main(void) {
=======
#ifdef CONFIG_ZMK_MOUSE
#include <zmk/mouse.h>
#endif /* CONFIG_ZMK_MOUSE */

#ifdef CONFIG_ZMK_POINT_DEVICE
#include <zmk/point_device.h>
#endif /* CONFIG_ZMK_POINT_DEVICE */

#define ZMK_KSCAN_DEV DT_LABEL(ZMK_MATRIX_NODE_ID)

void main(void) {
>>>>>>> 5591ade36fef72969c7328b61dd0da901d713048
    LOG_INF("Welcome to ZMK!\n");

    if (zmk_kscan_init(DEVICE_DT_GET(ZMK_MATRIX_NODE_ID)) != 0) {
        return -ENOTSUP;
    }

#ifdef CONFIG_ZMK_DISPLAY
    zmk_display_init();
#endif /* CONFIG_ZMK_DISPLAY */

<<<<<<< HEAD
    return 0;
=======
#ifdef CONFIG_ZMK_MOUSE
    zmk_mouse_init();
#endif /* CONFIG_ZMK_MOUSE */

#ifdef CONFIG_ZMK_POINT_DEVICE
    zmk_pd_init();
#endif /* CONFIG_ZMK_POINT_DEVICE */
>>>>>>> 5591ade36fef72969c7328b61dd0da901d713048
}
