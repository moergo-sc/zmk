zephyr_library()
zephyr_library_sources_ifdef(CONFIG_PMW33XX trackball.c)
zephyr_library_sources_ifdef(CONFIG_PMW3360 trackball_new.c)
zephyr_library_sources_ifdef(CONFIG_PAW3395 trackball_new.c)
zephyr_library_sources_ifdef(CONFIG_PMW3610 trackball_new.c)

zephyr_library_include_directories(${CMAKE_SOURCE_DIR}/include)
zephyr_library_include_directories(${CMAKE_SOURCE_DIR}/drivers/sensor)
zephyr_library_include_directories(${ZEPHYR_BASE}/drivers)
