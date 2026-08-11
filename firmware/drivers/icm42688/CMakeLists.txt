#
# ICM-42688-P Driver CMakeLists.txt
#

if(CONFIG_SENSORS_ICM42688)

nuttx_add_library(icm42688 STATIC)

target_sources(icm42688 PRIVATE
  icm42688.c
  icm42688_uorb.c
)

target_include_directories(icm42688 PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
)

endif()
