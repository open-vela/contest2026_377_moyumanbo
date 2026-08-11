# MAX17048 Battery Fuel Gauge Driver - CMake Build Script
#
# Copyright (c) 2026 VelaSense Project
# SPDX-License-Identifier: MIT

# ---------------------------------------------------------------------------
# Guard: only build when enabled via Kconfig / CMake option
# ---------------------------------------------------------------------------
if(CONFIG_MAX17048)

# ---------------------------------------------------------------------------
# Source files
# ---------------------------------------------------------------------------
set(MAX17048_SOURCES
    max17048.c
)

set(MAX17048_HEADERS
    max17048.h
)

# ---------------------------------------------------------------------------
# Library target
# ---------------------------------------------------------------------------
add_library(max17048 STATIC ${MAX17048_SOURCES})

target_include_directories(max17048
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

# ---------------------------------------------------------------------------
# Link-time dependencies (platform I2C / HAL)
# ---------------------------------------------------------------------------
# The driver itself is hardware-agnostic via the max17048_i2c_ops_t
# abstraction.  Consumers supply their own I2C implementation at runtime.
# If a platform I2C abstraction target exists, link it here for the
# convenience of downstream consumers:
if(TARGET vela_i2c_hal)
    target_link_libraries(max17048 PUBLIC vela_i2c_hal)
endif()

# ---------------------------------------------------------------------------
# Compile definitions from Kconfig
# ---------------------------------------------------------------------------
target_compile_definitions(max17048 PRIVATE
    MAX17048_CFG_I2C_BUS="${CONFIG_MAX17048_I2C_BUS}"
    MAX17048_CFG_I2C_ADDR=${CONFIG_MAX17048_I2C_ADDRESS}
    MAX17048_CFG_ALERT_THRESH=${CONFIG_MAX17048_ALERT_THRESHOLD}
    MAX17048_CFG_CRIT_THRESH=${CONFIG_MAX17048_CRIT_THRESHOLD}
    MAX17048_CFG_VOLT_LOW=${CONFIG_MAX17048_VOLTAGE_LOW_MV}
    MAX17048_CFG_VOLT_HIGH=${CONFIG_MAX17048_VOLTAGE_HIGH_MV}
    MAX17048_CFG_RESET_VOLT=${CONFIG_MAX17048_RESET_VOLTAGE_MV}
    MAX17048_CFG_READ_INT=${CONFIG_MAX17048_READ_INTERVAL_S}
    MAX17048_CFG_RCOMP=${CONFIG_MAX17048_DEFAULT_RCOMP}
)

if(CONFIG_MAX17048_ENABLE_HIBERNATE)
    target_compile_definitions(max17048 PRIVATE
        MAX17048_CFG_HIBRT=${CONFIG_MAX17048_HIBRT_THRESHOLD}
        MAX17048_CFG_ACTRT=${CONFIG_MAX17048_ACTRT_THRESHOLD}
    )
endif()

if(CONFIG_MAX17048_ENABLE_QUICKSTART)
    target_compile_definitions(max17048 PRIVATE MAX17048_CFG_QUICKSTART=1)
endif()

# ---------------------------------------------------------------------------
# Compiler hardening flags
# ---------------------------------------------------------------------------
target_compile_options(max17048 PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -Werror=format-security
    -fstack-protector-strong
)

# ---------------------------------------------------------------------------
# Install rules (for standalone SDK packaging)
# ---------------------------------------------------------------------------
install(FILES ${MAX17048_HEADERS}
    DESTINATION include/drivers/max17048
)

install(TARGETS max17048
    ARCHIVE DESTINATION lib
)

else()
    message(STATUS "MAX17048 driver disabled (CONFIG_MAX17048 is not set)")
endif()
