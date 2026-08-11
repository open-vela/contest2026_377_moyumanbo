# CMakeLists.txt — VelaSense event storage and encryption module
#
# Provides:
#   event_store   — Encrypted event storage on LittleFS
#   event_crypto  — AES-128-GCM encryption helpers
#   event_ble     — BLE GATT event sync
#
# Usage:
#   add_subdirectory(libs/event)
#   target_link_libraries(my_app PRIVATE velasense_event)
#
# Crypto backend selection (define ONE):
#   -DVELASENSE_USE_MBEDTLS=ON   Link mbedTLS (recommended for NuttX)
#   -DVELASENSE_USE_TINYCRYPT=ON Link tinycrypt (lightweight alternative)
#   Neither                       Use built-in stubs (NOT for production)

cmake_minimum_required(VERSION 3.16)

# ------------------------------------------------------------------ #
# Options                                                            #
# ------------------------------------------------------------------ #

option(VELASENSE_USE_MBEDTLS   "Use mbedTLS for AES-GCM and HKDF"  OFF)
option(VELASENSE_USE_TINYCRYPT "Use tinycrypt for crypto primitives" OFF)

# ------------------------------------------------------------------ #
# Sources                                                            #
# ------------------------------------------------------------------ #

set(EVENT_SOURCES
    event_store.c
    event_crypto.c
    event_ble.c
)

set(EVENT_HEADERS
    event_store.h
    event_crypto.h
    event_ble.h
)

# ------------------------------------------------------------------ #
# Library target                                                     #
# ------------------------------------------------------------------ #

add_library(velasense_event STATIC ${EVENT_SOURCES})

target_include_directories(velasense_event
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include/event>
)

# C standard — required for designated initializers, _Static_assert
target_compile_features(velasense_event PUBLIC c_std_11)

# ------------------------------------------------------------------ #
# Crypto backend                                                     #
# ------------------------------------------------------------------ #

if(VELASENSE_USE_MBEDTLS)
    target_compile_definitions(velasense_event
        PUBLIC VELASENSE_USE_MBEDTLS
    )
    # Find and link mbedTLS (NuttX usually provides it via package manager)
    find_package(MbedTLS QUIET)
    if(MbedTLS_FOUND)
        target_link_libraries(velasense_event PRIVATE MbedTLS::mbedtls)
    else()
        # Fallback: assume mbedTLS headers and library are on the link path
        target_link_libraries(velasense_event PRIVATE mbedtls mbedx509 mbedcrypto)
    endif()
    message(STATUS "VelaSense event: using mbedTLS crypto backend")

elseif(VELASENSE_USE_TINYCRYPT)
    target_compile_definitions(velasense_event
        PUBLIC VELASENSE_USE_TINYCRYPT
    )
    # tinycrypt is usually pulled in as a submodule or NuttX package
    find_library(TINYCRYPT_LIB tinycrypt)
    if(TINYCRYPT_LIB)
        target_link_libraries(velasense_event PRIVATE ${TINYCRYPT_LIB})
    else()
        target_link_libraries(velasense_event PRIVATE tinycrypt)
    endif()
    message(STATUS "VelaSense event: using tinycrypt crypto backend")

else()
    message(STATUS "VelaSense event: using STUB crypto backend (NOT for production)")
endif()

# ------------------------------------------------------------------ #
# NuttX POSIX compatibility                                          #
# ------------------------------------------------------------------ #

# NuttX provides a POSIX layer but sometimes needs explicit flags
if(CMAKE_SYSTEM_NAME STREQUAL "NuttX")
    target_compile_definitions(velasense_event PRIVATE
        _POSIX_C_SOURCE=200809L
        __NUTTX__
    )
endif()

# ------------------------------------------------------------------ #
# Warnings                                                           #
# ------------------------------------------------------------------ #

target_compile_options(velasense_event PRIVATE
    -Wall
    -Wextra
    -Wno-unused-parameter
    -Werror=return-type
)

# ------------------------------------------------------------------ #
# Install (optional)                                                 #
# ------------------------------------------------------------------ #

install(TARGETS velasense_event
    ARCHIVE DESTINATION lib
)

install(FILES ${EVENT_HEADERS}
    DESTINATION include/event
)
