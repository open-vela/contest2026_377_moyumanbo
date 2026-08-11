#
# VelaSense Feature Extraction Module
#
# Extracts 15 physiological features from sensor data for
# emotion arousal inference.
#

if(CONFIG_VELASENSE)

nuttx_add_library(features)

target_sources(features
  PRIVATE
    feature_extract.c
    baseline.c
    activity_classify.c
)

target_include_directories(features
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(features
  PRIVATE
    m
)

endif()
