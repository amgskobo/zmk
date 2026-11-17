# SPDX-License-Identifier: MIT
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(studio_discovery)

set(STUDIO_CORE_SOURCE "${CMAKE_CURRENT_LIST_DIR}/../../src/studio/core.c"
    CACHE FILEPATH "Production core, or explicitly selected original-source control")
target_include_directories(app BEFORE PRIVATE include ../../include)
option(STUDIO_DISCOVERY_ENABLED "Exercise enabled discovery" ON)
set(STUDIO_IDLE_TIMEOUT_SEC 180 CACHE STRING "Lock timeout for configuration boundary checks")
target_compile_definitions(app PRIVATE
    CONFIG_ZMK_STUDIO_LOCKING=1 CONFIG_ZMK_STUDIO_LOCK_IDLE_TIMEOUT_SEC=${STUDIO_IDLE_TIMEOUT_SEC}
    CONFIG_ZMK_STUDIO_LOCK_ON_DISCONNECT=1
    CONFIG_ZMK_STUDIO_BLE_ADVERTISING_TIMEOUT_SEC=90
    CONFIG_ZMK_STUDIO_LOG_LEVEL=0 CONFIG_ZMK_STUDIO_TRANSPORT_BLE_PREF_LATENCY=0
    CONFIG_BT_PERIPHERAL_PREF_LATENCY=0 CONFIG_BT_USER_DATA_LEN_UPDATE=1)
target_sources(app PRIVATE main.c "${STUDIO_CORE_SOURCE}")
if(STUDIO_DISCOVERY_ENABLED)
    target_compile_definitions(app PRIVATE CONFIG_ZMK_STUDIO_LOCK_BLE_DIRECT_ADVERTISING_ON_UNLOCK=1)
    target_sources(app PRIVATE ../../src/studio/ble_discovery.c)
endif()
