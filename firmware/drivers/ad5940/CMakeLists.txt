#
# AD5940 EDA Analog Frontend Driver CMakeLists.txt
#

if(CONFIG_SENSORS_AD5940)

nuttx_add_library(ad5940 STATIC)

target_sources(ad5940 PRIVATE
  ad5940.c
)

if(CONFIG_AD5940_UORB_ENABLE)
  target_sources(ad5940 PRIVATE
    ad5940_uorb.c
  )
endif()

target_include_directories(ad5940 PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
)

endif()
