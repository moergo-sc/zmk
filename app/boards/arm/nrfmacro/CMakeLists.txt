#
# Copyright (c) 2021 Darryl deHaan
# SPDX-License-Identifier: MIT
#

if(CONFIG_ZMK_DISPLAY AND CONFIG_ZMK_DISPLAY_STATUS_SCREEN_CUSTOM)
  add_subdirectory_ifdef(CONFIG_NRFMACRO_EPD_DISPLAY epd_screen)
  # add_subdirectory_ifdef(CONFIG_NRFMACRO_LCD_DISPLAY lcd_screen)
  # add_subdirectory_ifdef(CONFIG_NRFMACRO_OLED_DISPLAY oled_screen)
endif()

if (CONFIG_NRFMACRO_POINTDEVICE)
  add_subdirectory_ifdef(CONFIG_PINNACLE trackpad)
  add_subdirectory_ifdef(CONFIG_PMW33XX trackball)
  add_subdirectory_ifdef(CONFIG_PMW3360 trackball)
  add_subdirectory_ifdef(CONFIG_PAW3395 trackball)
  add_subdirectory_ifdef(CONFIG_PMW3610 trackball)
endif()
