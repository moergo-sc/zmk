/*
 * Copyright (c) 2020 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "peripheral_status.h"
#include "battery_status.h"
#include <zmk/display/status_screen.h>

#include <logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <zmk/display.h>
LV_IMG_DECLARE(marklogo);
#if IS_ENABLED(CONFIG_NRFMACRO_SCREEN_STANDARD_LOGO)
LV_IMG_DECLARE(stdlogo);
#endif
#if IS_ENABLED(CONFIG_NRFMACRO_SCREEN_CUSTOM_LOGO)
LV_IMG_DECLARE(customlogo);
#endif

#if IS_ENABLED(CONFIG_CUSTOM_WIDGET_BATTERY_STATUS)
static struct zmk_widget_battery_status battery_status_widget;
#endif

#if IS_ENABLED(CONFIG_CUSTOM_WIDGET_PERIPHERAL_STATUS)
static struct zmk_widget_peripheral_status peripheral_status_widget;
#endif

lv_obj_t *zmk_display_status_screen() {
    lv_obj_t *screen;

    screen = lv_obj_create(NULL, NULL);

#if IS_ENABLED(CONFIG_CUSTOM_WIDGET_BATTERY_STATUS)
    zmk_widget_battery_status_init(&battery_status_widget, screen);
    lv_obj_align(zmk_widget_battery_status_obj(&battery_status_widget), NULL, LV_ALIGN_IN_TOP_RIGHT,
                 -CONFIG_NRFMACRO_SCREEN_RIGHT_MARGIN, CONFIG_NRFMACRO_SCREEN_TOP_MARGIN);
#endif

#if IS_ENABLED(CONFIG_CUSTOM_WIDGET_PERIPHERAL_STATUS)
    zmk_widget_peripheral_status_init(&peripheral_status_widget, screen);
    lv_obj_align(zmk_widget_peripheral_status_obj(&peripheral_status_widget), NULL,
                 LV_ALIGN_IN_TOP_LEFT, CONFIG_NRFMACRO_SCREEN_LEFT_MARGIN, CONFIG_NRFMACRO_SCREEN_TOP_MARGIN);
#endif

    // todo:
    // 1. a product mark line (sweep-pro)
#if IS_ENABLED(CONFIG_NRFMACRO_SCREEN_MARK_LOGO)
    lv_obj_t * marklogo_icon;
    marklogo_icon = lv_img_create(screen, NULL);
    lv_img_set_src(marklogo_icon, &marklogo);
    lv_obj_align(marklogo_icon, NULL, LV_ALIGN_IN_BOTTOM_MID, CONFIG_NRFMACRO_SCREEN_LEFT_MARGIN, -CONFIG_NRFMACRO_SCREEN_BOTTOM_MARGIN);
#endif
    // 2. a product logo
#if IS_ENABLED(CONFIG_NRFMACRO_SCREEN_STANDARD_LOGO)
    lv_obj_t * stdlogo_icon;
    stdlogo_icon = lv_img_create(screen, NULL);
    lv_img_set_src(stdlogo_icon, &stdlogo);
    lv_obj_align(stdlogo_icon, NULL, LV_ALIGN_IN_BOTTOM_MID, 0, -28-CONFIG_NRFMACRO_SCREEN_BOTTOM_MARGIN);
#endif
    // 3. configurable personal logo, which can replace the product logo
#if IS_ENABLED(CONFIG_NRFMACRO_SCREEN_CUSTOM_LOGO)
    lv_obj_t * customlogo_icon;
    customlogo_icon = lv_img_create(screen, NULL);
    lv_img_set_src(customlogo_icon, &customlogo);
    lv_obj_align(customlogo_icon, NULL, LV_ALIGN_IN_BOTTOM_MID, 0, -28-CONFIG_NRFMACRO_SCREEN_BOTTOM_MARGIN);
#endif

    lv_refr_now(NULL);

    return screen;
}
