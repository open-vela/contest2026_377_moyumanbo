# VelaSense BLE service build script
#
# SPDX-License-Identifier: Apache-2.0
#
# Copyright (C) 2026 VelaSense Project

# Source files for the BLE service

set(SRCS
  gatt_service.c
  ble_sync.c
  ble_manager.c
)

# Create the BLE service library

nuttx_add_library(velasense_ble STATIC ${SRCS})

# Include paths

target_include_directories(velasense_ble
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Link dependencies

target_link_libraries(velasense_ble
  PRIVATE
    nuttx_bluetooth
)

# Include paths for sibling modules (event state machine, sensor manager)

target_include_directories(velasense_ble
  PRIVATE
    ${CMAKE_SOURCE_DIR}/firmware/libs/event
    ${CMAKE_SOURCE_DIR}/firmware/drivers/common
)

# Compiler flags

target_compile_options(velasense_ble
  PRIVATE
    -Wall
    -Wextra
    -Wno-unused-parameter
)

# Register the module with the build system

nuttx_register_driver(
  NAME velasense_ble
  DEPENDS "CONFIG_VELASENSE_BLE"
)
