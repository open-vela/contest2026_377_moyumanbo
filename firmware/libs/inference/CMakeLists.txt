# ===========================================================================
# CMakeLists.txt -- VelaSense TinyML Inference Engine
#
# Build target: velasense_inference
#
# This library compiles the INT8 quantised inference engine and the
# rule-based gating layer into a single static library that can be
# linked into any Cortex-M firmware image.
#
# Usage (standalone):
#   mkdir build && cd build
#   cmake -DCMAKE_TOOLCHAIN_FILE=../../cmake/arm-none-eabi.cmake ..
#   make
#
# Usage (as subdirectory):
#   add_subdirectory(firmware/libs/inference)
#   target_link_libraries(my_app PRIVATE velasense_inference)
# ===========================================================================

cmake_minimum_required(VERSION 3.16)

# ---- Guard against in-source builds ---------------------------------------
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(FATAL_ERROR
        "In-source builds are not allowed. "
        "Create a build/ directory and run cmake from there.")
endif()

# ---- Library target -------------------------------------------------------
add_library(velasense_inference STATIC

    # Core inference engine
    tinyml.c

    # Model weights and metadata (placeholder -- replace with trained weights)
    model_data.c

    # Rule-based gating
    rules_gate.c
)

# ---- Public headers -------------------------------------------------------
target_include_directories(velasense_inference
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

# ---- Compiler flags (target-agnostic defaults; the toolchain file
#      should set -mcpu=cortex-m33 -mthumb -mfpu=fpv5-sp-d16 etc.) -------
target_compile_options(velasense_inference PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -Werror
    -Wno-unused-parameter
    -std=c11
    -ffunction-sections
    -fdata-sections
)

# Optimise for size on embedded targets (can be overridden by the parent)
if(NOT CMAKE_BUILD_TYPE)
    set_target_properties(velasense_inference PROPERTIES
        COMPILE_FLAGS "-Os"
    )
endif()

# ---- Kconfig integration --------------------------------------------------
# When built inside a Zephyr/NuttX project the Kconfig symbols are available
# as CONFIG_*.  Map them to compile definitions so the C code can use #ifdef.

if(CONFIG_VELASENSE_INFERENCE)
    target_compile_definitions(velasense_inference PUBLIC
        VELASENSE_INFERENCE_ENABLED=1
    )
endif()

if(CONFIG_VELASENSE_RULE_GATE)
    target_compile_definitions(velasense_inference PUBLIC
        VELASENSE_RULE_GATE_ENABLED=1
    )
endif()

if(CONFIG_TINYML_DEBUG)
    target_compile_definitions(velasense_inference PUBLIC
        TINYML_DEBUG=1
    )
endif()

if(CONFIG_TINYML_TIMING)
    target_compile_definitions(velasense_inference PUBLIC
        TINYML_TIMING=1
    )
endif()

# Rule gate threshold overrides from Kconfig
if(DEFINED CONFIG_RULE_SQI_MIN)
    target_compile_definitions(velasense_inference PRIVATE
        CONFIG_RULE_SQI_MIN=${CONFIG_RULE_SQI_MIN}
    )
endif()

if(DEFINED CONFIG_RULE_TEMP_SLOPE_MAX)
    target_compile_definitions(velasense_inference PRIVATE
        CONFIG_RULE_TEMP_SLOPE_MAX=${CONFIG_RULE_TEMP_SLOPE_MAX}
    )
endif()

if(DEFINED CONFIG_RULE_ACTIVITY_MAX)
    target_compile_definitions(velasense_inference PRIVATE
        CONFIG_RULE_ACTIVITY_MAX=${CONFIG_RULE_ACTIVITY_MAX}
    )
endif()

if(DEFINED CONFIG_RULE_BATTERY_MIN)
    target_compile_definitions(velasense_inference PRIVATE
        CONFIG_RULE_BATTERY_MIN=${CONFIG_RULE_BATTERY_MIN}
    )
endif()

# ---- Linker section for flash-resident weights ----------------------------
# The model weights should live in a dedicated flash section so the linker
# script can place them in a read-only region.  This is a hint; the actual
# section placement depends on the platform linker script.
if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    set_target_properties(velasense_inference PROPERTIES
        COMPILE_FLAGS "${COMPILE_FLAGS} -fno-common"
    )
endif()

# ---- Install rule (optional) ----------------------------------------------
install(TARGETS velasense_inference
    ARCHIVE DESTINATION lib
)

install(FILES
    tinyml.h
    model_data.h
    rules_gate.h
    DESTINATION include/velasense
)
