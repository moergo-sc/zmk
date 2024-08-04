# Copyright (c) 2022 The ZMK Contributors
# SPDX-License-Identifier: MIT

zephyr_library()
zephyr_library_include_directories(.)
zephyr_library_include_directories(${CMAKE_SOURCE_DIR}/include)


zephyr_library_sources(cirque_trackpad.c)
