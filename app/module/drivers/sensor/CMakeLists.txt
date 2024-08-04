# Copyright (c) 2020-2021 The ZMK Contributors
# SPDX-License-Identifier: MIT

add_subdirectory_ifdef(CONFIG_ZMK_BATTERY battery)
add_subdirectory_ifdef(CONFIG_EC11 ec11)
<<<<<<< HEAD:app/module/drivers/sensor/CMakeLists.txt
add_subdirectory_ifdef(CONFIG_ZMK_MAX17048 max17048)
=======
add_subdirectory_ifdef(CONFIG_PMW33XX pmw33xx)
add_subdirectory_ifdef(CONFIG_PINNACLE cirque_trackpad)

# new impl
add_subdirectory_ifdef(CONFIG_PMW3360 pixart/pmw3360)
add_subdirectory_ifdef(CONFIG_PAW3395 pixart/paw3395)
add_subdirectory_ifdef(CONFIG_PMW3610 pixart/pmw3610)
>>>>>>> 5591ade36fef72969c7328b61dd0da901d713048:app/drivers/sensor/CMakeLists.txt
