# MAX86141 PPG sensor driver build script
#
# SPDX-License-Identifier: Apache-2.0
#
# Copyright (C) 2026 VelaSense Project

# Source files for the MAX86141 driver

set(SRCS max86141.c)

# Add uORB integration if enabled

if(CONFIG_MAX86141_UORB)
  list(APPEND SRCS max86141_uorb.c)
endif()

# Create the driver library

nuttx_add_library(max86141 STATIC ${SRCS})

# Include paths

target_include_directories(max86141
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Link dependencies

target_link_libraries(max86141
  PRIVATE
    nuttx_spi
    nuttx_gpio
)

# Add uORB dependency if enabled

if(CONFIG_MAX86141_UORB)
  target_link_libraries(max86141
    PRIVATE
      nuttx_uorb
  )
endif()

# Compiler flags specific to this driver

target_compile_options(max86141
  PRIVATE
    -Wall
    -Wextra
    -Wno-unused-parameter
)

# Register the driver with the build system

nuttx_register_driver(
  NAME max86141
  DEPENDS "CONFIG_MAX86141"
)
