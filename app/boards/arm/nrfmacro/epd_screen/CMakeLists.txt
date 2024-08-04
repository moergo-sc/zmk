#
# Copyright (c) 2021 Darryl deHaan
# SPDX-License-Identifier: MIT
#

add_subdirectory_ifdef(CONFIG_NRFMACRO_SHIELD_MASTER master)
add_subdirectory_ifdef(CONFIG_NRFMACRO_SHIELD_SLAVE slave)
