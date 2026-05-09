# Copyright (c) 2024 The ZMK Contributors
# SPDX-License-Identifier: MIT

zephyr_library_amend()

zephyr_library_sources_ifdef(CONFIG_INPUT_PINNACLE input_pinnacle.c)
zephyr_library_sources_ifdef(CONFIG_INPUT_PINNACLE_STUDIO_RPC pinnacle_studio_rpc.c)
zephyr_library_sources_ifdef(CONFIG_INPUT_PINNACLE_STUDIO_RPC proto/dya/trackpad/trackpad.pb.c)

target_sources_ifdef(CONFIG_ZMK_INPUT_PINNACLE_IDLE_SLEEPER app PRIVATE zmk_pinnacle_idle_sleeper.c)

if(CONFIG_INPUT_PINNACLE_STUDIO_RPC)
    zephyr_library_include_directories(${CMAKE_CURRENT_SOURCE_DIR})
    zephyr_library_include_directories(${CMAKE_CURRENT_SOURCE_DIR}/proto/dya/trackpad)
endif()
