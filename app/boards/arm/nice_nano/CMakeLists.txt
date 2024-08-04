#
# Copyright (c) 2021 Darryl deHaan
# SPDX-License-Identifier: MIT
#

if (CONFIG_NICE_NANO_POINTDEVICE)
  add_subdirectory_ifdef(CONFIG_PINNACLE trackpad)
  add_subdirectory_ifdef(CONFIG_PMW33XX trackball)
  add_subdirectory_ifdef(CONFIG_PMW3360 trackball)
  add_subdirectory_ifdef(CONFIG_PAW3395 trackball)
  add_subdirectory_ifdef(CONFIG_PMW3610 trackball)
endif()