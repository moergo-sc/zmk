#
# Copyright (c) 2021 Darryl deHaan
# SPDX-License-Identifier: MIT
#

zephyr_library()

zephyr_library_include_directories(${ZEPHYR_LVGL_MODULE_DIR})
zephyr_library_include_directories(${ZEPHYR_BASE}/lib/gui/lvgl/)
zephyr_library_include_directories(${CMAKE_SOURCE_DIR}/include)
zephyr_library_include_directories(${ZEPHYR_BASE}/drivers)

zephyr_library_sources(status_screen.c)
zephyr_library_sources_ifdef(CONFIG_CUSTOM_WIDGET_BATTERY_STATUS battery_status.c)
zephyr_library_sources_ifdef(CONFIG_CUSTOM_WIDGET_PERIPHERAL_STATUS peripheral_status.c)

add_compile_definitions(LV_LVGL_H_INCLUDE_SIMPLE)
if (CONFIG_NRFMACRO_SCREEN_MARK_LOGO)
zephyr_library_sources(icons/${CONFIG_NRFMACRO_MARKLOGO_NAME})
endif()

if (CONFIG_NRFMACRO_SCREEN_STANDARD_LOGO)
zephyr_library_sources(icons/stdlogo.c)
endif()

if (CONFIG_NRFMACRO_SCREEN_CUSTOM_LOGO)
if(ZMK_CONFIG)
  zephyr_library_sources(${ZMK_CONFIG}/icons/customlogo.c)
else()
  zephyr_library_sources(icons/customlogo.c)
endif()
endif()

