#
# VelaSense Common Sensor Framework
#
# Provides shared infrastructure for all sensor drivers:
#   - sensor_sync: Unified microsecond timestamp system
#   - ring_buffer: Lock-free SPSC ring buffer
#   - sensor_manager: Multi-sensor orchestrator
#

if(CONFIG_SENSORS_VELASENSE)

nuttx_add_library(velasense_common STATIC)

target_sources(velasense_common PRIVATE
  sensor_sync.c
  ring_buffer.c
  sensor_manager.c
)

target_include_directories(velasense_common PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
)

endif()
