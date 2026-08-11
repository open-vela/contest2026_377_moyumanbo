#
# MAX30208 Driver CMakeLists.txt
#

if(CONFIG_SENSORS_MAX30208)

nuttx_add_library(max30208 STATIC)

target_sources(max30208 PRIVATE
  max30208.c
  max30208_uorb.c
)

target_include_directories(max30208 PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
)

endif()
