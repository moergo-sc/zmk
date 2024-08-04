# Copyright (c) 2020-2023 The ZMK Contributors
# SPDX-License-Identifier: MIT

zephyr_library_amend()

<<<<<<< HEAD:app/module/drivers/kscan/CMakeLists.txt
zephyr_library_sources_ifdef(CONFIG_ZMK_KSCAN_GPIO_DRIVER kscan_gpio.c)
=======
zephyr_library_sources(kscan_wrapper.c)
zephyr_library_sources_ifdef(CONFIG_ZMK_KSCAN_GPIO_DRIVER debounce.c)
>>>>>>> 5591ade36fef72969c7328b61dd0da901d713048:app/drivers/kscan/CMakeLists.txt
zephyr_library_sources_ifdef(CONFIG_ZMK_KSCAN_GPIO_MATRIX kscan_gpio_matrix.c)
zephyr_library_sources_ifdef(CONFIG_ZMK_KSCAN_GPIO_CHARLIEPLEX kscan_gpio_charlieplex.c)
zephyr_library_sources_ifdef(CONFIG_ZMK_KSCAN_GPIO_DIRECT kscan_gpio_direct.c)
zephyr_library_sources_ifdef(CONFIG_ZMK_KSCAN_GPIO_DEMUX kscan_gpio_demux.c)
zephyr_library_sources_ifdef(CONFIG_ZMK_KSCAN_MOCK_DRIVER kscan_mock.c)
zephyr_library_sources_ifdef(CONFIG_ZMK_KSCAN_COMPOSITE_DRIVER kscan_composite.c)
zephyr_library_sources_ifdef(CONFIG_ZMK_KSCAN_CAP1203	kscan_cap1203.c)
