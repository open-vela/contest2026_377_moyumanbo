# CMakeLists.txt — DRV2605L Haptic Motor Driver
# VelaSense Project
# SPDX-License-Identifier: MIT

# Guard: only build if the Kconfig symbol is enabled.
# The parent build system (Zephyr / NuttX / bare-metal CMake)
# is expected to define CONFIG_DRV2605L when the Kconfig option
# is selected.  For standalone builds the guard is relaxed.
if(DEFINED CONFIG_DRV2605L AND NOT CONFIG_DRV2605L)
    return()
endif()

# ------------------------------------------------------------------ #
#  Source files                                                        #
# ------------------------------------------------------------------ #

set(DRV2605L_SOURCES
    drv2605l.c
)

set(DRV2605L_HEADERS
    drv2605l.h
)

# ------------------------------------------------------------------ #
#  Target library                                                     #
# ------------------------------------------------------------------ #

add_library(drv2605l STATIC ${DRV2605L_SOURCES})

target_include_directories(drv2605l
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

# ------------------------------------------------------------------ #
#  Compiler flags                                                     #
# ------------------------------------------------------------------ #

# Pedantic warnings so the driver stays clean
target_compile_options(drv2605l PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -Wno-unused-parameter
)

# C standard: require at least C99 (for stdint.h, stdbool.h, inline)
set_target_properties(drv2605l PROPERTIES
    C_STANDARD          99
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS        OFF
)

# ------------------------------------------------------------------ #
#  Kconfig-driven compile definitions                                 #
# ------------------------------------------------------------------ #

# I2C address
if(DEFINED CONFIG_DRV2605L_I2C_ADDR)
    target_compile_definitions(drv2605l PRIVATE
        DRV2605L_CFG_I2C_ADDR=${CONFIG_DRV2605L_I2C_ADDR}
    )
endif()

# Motor type
if(CONFIG_DRV2605L_MOTOR_LRA)
    target_compile_definitions(drv2605l PRIVATE DRV2605L_CFG_MOTOR_LRA=1)
elseif(CONFIG_DRV2605L_MOTOR_ERM)
    target_compile_definitions(drv2605l PRIVATE DRV2605L_CFG_MOTOR_LRA=0)
endif()

# Waveform library
if(DEFINED CONFIG_DRV2605L_LIBRARY)
    target_compile_definitions(drv2605l PRIVATE
        DRV2605L_CFG_LIBRARY=${CONFIG_DRV2605L_LIBRARY}
    )
endif()

# Rated voltage
if(DEFINED CONFIG_DRV2605L_RATED_VOLTAGE)
    target_compile_definitions(drv2605l PRIVATE
        DRV2605L_CFG_RATED_VOLTAGE=${CONFIG_DRV2605L_RATED_VOLTAGE}
    )
endif()

# Overdrive clamp
if(DEFINED CONFIG_DRV2605L_OD_CLAMP)
    target_compile_definitions(drv2605l PRIVATE
        DRV2605L_CFG_OD_CLAMP=${CONFIG_DRV2605L_OD_CLAMP}
    )
endif()

# BEMF gain
if(DEFINED CONFIG_DRV2605L_BEMF_GAIN)
    target_compile_definitions(drv2605l PRIVATE
        DRV2605L_CFG_BEMF_GAIN=${CONFIG_DRV2605L_BEMF_GAIN}
    )
endif()

# Auto-calibration
if(CONFIG_DRV2605L_AUTO_CALIBRATE)
    target_compile_definitions(drv2605l PRIVATE DRV2605L_CFG_AUTO_CAL=1)
else()
    target_compile_definitions(drv2605l PRIVATE DRV2605L_CFG_AUTO_CAL=0)
endif()

# Playback timeout
if(DEFINED CONFIG_DRV2605L_TIMEOUT_MS)
    target_compile_definitions(drv2605l PRIVATE
        DRV2605L_CFG_TIMEOUT_MS=${CONFIG_DRV2605L_TIMEOUT_MS}
    )
endif()

# Debug logging
if(CONFIG_DRV2605L_DEBUG)
    target_compile_definitions(drv2605l PRIVATE DRV2605L_DEBUG=1)
endif()

# ------------------------------------------------------------------ #
#  Dependencies                                                       #
# ------------------------------------------------------------------ #

# The driver itself has no external library dependencies beyond the
# standard C library.  Platform I2C / GPIO / delay are provided at
# runtime through the drv2605l_platform_t function-pointer table.

# If building within Zephyr, link against the I2C subsystem:
if(TARGET zephyr_interface)
    target_link_libraries(drv2605l PRIVATE zephyr_interface)
endif()

# If building within NuttX, the board-level I2C library is linked
# by the application — no additional dependency needed here.

# ------------------------------------------------------------------ #
#  Export for parent build                                            #
# ------------------------------------------------------------------ #

# Make the include path and library available to consuming targets
set(DRV2605L_INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR} PARENT_SCOPE)
set(DRV2605L_LIBRARIES    drv2605l                       PARENT_SCOPE)
