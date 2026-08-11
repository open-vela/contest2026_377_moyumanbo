# VelaSense DSP Library Build Script
#
# Digital signal processing modules for PPG-based heart rate monitoring:
#   - ppg_filter:   Bandpass filter (0.5-5 Hz Butterworth)
#   - ppg_sqi:      Signal quality index
#   - hrv:          Heart rate variability analysis
#   - peak_detect:  Adaptive peak detection for PPG
#   - motion_artifact: IMU-reference motion artifact suppression
#
# SPDX-License-Identifier: Apache-2.0
#
# Copyright (C) 2026 VelaSense Project

# Source files for the DSP library

set(SRCS
  ppg_filter.c
  ppg_sqi.c
  hrv.c
  peak_detect.c
  motion_artifact.c
)

# Create the static library

nuttx_add_library(velasense_dsp STATIC ${SRCS})

# Include paths — public headers visible to consumers

target_include_directories(velasense_dsp
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Compiler flags

target_compile_options(velasense_dsp
  PRIVATE
    -Wall
    -Wextra
    -Wno-unused-parameter
)

# Link math library (required for sinf, cosf, sqrtf, log10f, fabsf)

target_link_libraries(velasense_dsp
  PRIVATE
    m
)
