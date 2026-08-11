###############################################################################
# VelaSense Test Suite — CMake Build Script
#
# Builds unit tests, integration tests, and the privacy audit runner.
#
# Usage:
#   mkdir build && cd build
#   cmake .. -DCMAKE_BUILD_TYPE=Debug
#   make -j$(nproc)
#   ctest --output-on-failure
#
###############################################################################

cmake_minimum_required(VERSION 3.14)
project(VelaSenseTests C)

###############################################################################
# Compiler settings
###############################################################################

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Werror -g -O0")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-unused-parameter")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-missing-field-initializers")

# Define _POSIX_C_SOURCE for clock_gettime and related functions

add_definitions(-D_POSIX_C_SOURCE=200809L)
add_definitions(-DTEST_BUILD)

###############################################################################
# Find CMocka
###############################################################################

find_library(CMOCKA_LIBRARY cmocka
    PATHS /usr/lib /usr/lib/x86_64-linux-gnu /usr/local/lib
)

find_path(CMOCKA_INCLUDE_DIR cmocka.h
    PATHS /usr/include /usr/local/include
)

if(NOT CMOCKA_LIBRARY)
    message(FATAL_ERROR
        "CMocka library not found. Install with:\n"
        "  Ubuntu/Debian: sudo apt-get install libcmocka-dev\n"
        "  macOS:         brew install cmocka\n"
        "  Fedora:        sudo dnf install libcmocka-devel"
    )
endif()

message(STATUS "CMocka library: ${CMOCKA_LIBRARY}")
message(STATUS "CMocka include: ${CMOCKA_INCLUDE_DIR}")

###############################################################################
# Source directories
###############################################################################

set(FIRMWARE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)
set(DSP_DIR ${FIRMWARE_ROOT}/libs/dsp)
set(EVENT_DIR ${FIRMWARE_ROOT}/libs/event)
set(FEATURES_DIR ${FIRMWARE_ROOT}/libs/features)
set(BLE_DIR ${FIRMWARE_ROOT}/ble)

###############################################################################
# DSP library sources (compiled into test executables)
###############################################################################

set(DSP_SOURCES
    ${DSP_DIR}/ppg_sqi.c
    ${DSP_DIR}/ppg_filter.c
    ${DSP_DIR}/hrv.c
)

# peak_detect.c is referenced but may not exist yet — make it optional

if(EXISTS ${DSP_DIR}/peak_detect.c)
    list(APPEND DSP_SOURCES ${DSP_DIR}/peak_detect.c)
endif()

###############################################################################
# Event library sources
###############################################################################

set(EVENT_SOURCES
    ${EVENT_DIR}/event_sm.c
)

# event_crypto.c is referenced but may not exist yet — make it optional

if(EXISTS ${EVENT_DIR}/event_crypto.c)
    list(APPEND EVENT_SOURCES ${EVENT_DIR}/event_crypto.c)
endif()

###############################################################################
# Feature library sources
###############################################################################

# activity_classify.c is referenced but may not exist yet — make it optional

set(FEATURE_SOURCES "")

if(EXISTS ${FEATURES_DIR}/activity_classify.c)
    list(APPEND FEATURE_SOURCES ${FEATURES_DIR}/activity_classify.c)
endif()

###############################################################################
# Include directories
###############################################################################

include_directories(
    ${CMOCKA_INCLUDE_DIR}
    ${DSP_DIR}
    ${EVENT_DIR}
    ${FEATURES_DIR}
    ${BLE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}
)

###############################################################################
# Mock implementations for platform functions
###############################################################################

# Create a mock library for functions that are platform-specific
# (e.g., clock_gettime, syslog, etc.) that may not be available
# in the test environment.

include(CheckFunctionExists)
set(MOCK_SOURCES "")

# Check if we need to mock clock_gettime

check_function_exists(clock_gettime HAS_CLOCK_GETTIME)

if(NOT HAS_CLOCK_GETTIME)
    # Create a mock for clock_gettime
    file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/mock_time.c "
#include <time.h>
#include <stdint.h>
static uint64_t mock_time_ns = 0;
int clock_gettime(int clk_id, struct timespec *tp) {
    tp->tv_sec = mock_time_ns / 1000000000ULL;
    tp->tv_nsec = mock_time_ns % 1000000000ULL;
    return 0;
}
void mock_advance_time_ns(uint64_t ns) {
    mock_time_ns += ns;
}
void mock_set_time_ns(uint64_t ns) {
    mock_time_ns = ns;
}
")
    list(APPEND MOCK_SOURCES ${CMAKE_CURRENT_BINARY_DIR}/mock_time.c)
endif()

###############################################################################
# Test: DSP Unit Tests
###############################################################################

add_executable(test_dsp
    test_dsp.c
    ${DSP_SOURCES}
    ${MOCK_SOURCES}
)

target_link_libraries(test_dsp ${CMOCKA_LIBRARY} m)

target_include_directories(test_dsp PRIVATE
    ${DSP_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}
)

add_test(NAME dsp_tests COMMAND test_dsp)

###############################################################################
# Test: Feature Extraction Tests
###############################################################################

add_executable(test_features
    test_features.c
    ${FEATURE_SOURCES}
    ${MOCK_SOURCES}
)

target_link_libraries(test_features ${CMOCKA_LIBRARY} m)

target_include_directories(test_features PRIVATE
    ${FEATURES_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}
)

add_test(NAME feature_tests COMMAND test_features)

###############################################################################
# Test: Event State Machine Tests
###############################################################################

add_executable(test_event_sm
    test_event_sm.c
    ${EVENT_SOURCES}
    ${MOCK_SOURCES}
)

target_link_libraries(test_event_sm ${CMOCKA_LIBRARY} m)

target_include_directories(test_event_sm PRIVATE
    ${EVENT_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}
)

add_test(NAME event_sm_tests COMMAND test_event_sm)

###############################################################################
# Test: Privacy Compliance Tests
###############################################################################

add_executable(test_privacy
    test_privacy.c
    ${EVENT_SOURCES}
    ${MOCK_SOURCES}
)

target_link_libraries(test_privacy ${CMOCKA_LIBRARY} m)

target_include_directories(test_privacy PRIVATE
    ${EVENT_DIR}
    ${BLE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}
)

add_test(NAME privacy_tests COMMAND test_privacy)

###############################################################################
# Test: Integration Tests
###############################################################################

add_executable(test_integration
    test_integration.c
    ${DSP_SOURCES}
    ${EVENT_SOURCES}
    ${FEATURE_SOURCES}
    ${MOCK_SOURCES}
)

target_link_libraries(test_integration ${CMOCKA_LIBRARY} m)

target_include_directories(test_integration PRIVATE
    ${DSP_DIR}
    ${EVENT_DIR}
    ${FEATURES_DIR}
    ${BLE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}
)

add_test(NAME integration_tests COMMAND test_integration)

###############################################################################
# CTest configuration
###############################################################################

enable_testing()

# Set test timeouts (seconds)

set_tests_properties(dsp_tests PROPERTIES TIMEOUT 30)
set_tests_properties(feature_tests PROPERTIES TIMEOUT 30)
set_tests_properties(event_sm_tests PROPERTIES TIMEOUT 30)
set_tests_properties(privacy_tests PROPERTIES TIMEOUT 30)
set_tests_properties(integration_tests PROPERTIES TIMEOUT 60)

###############################################################################
# Install (optional)
###############################################################################

install(TARGETS
    test_dsp
    test_features
    test_event_sm
    test_privacy
    test_integration
    RUNTIME DESTINATION bin
)

###############################################################################
# Summary
###############################################################################

message(STATUS "")
message(STATUS "VelaSense Test Suite Configuration:")
message(STATUS "  CMocka:    ${CMOCKA_LIBRARY}")
message(STATUS "  DSP src:   ${DSP_SOURCES}")
message(STATUS "  Event src: ${EVENT_SOURCES}")
message(STATUS "  Feature src: ${FEATURE_SOURCES}")
message(STATUS "")
message(STATUS "Build with: make -j\$(nproc)")
message(STATUS "Run tests:  ctest --output-on-failure")
message(STATUS "")
