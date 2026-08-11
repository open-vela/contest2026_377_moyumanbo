#
# VelaSense Application CMakeLists.txt
#

if(CONFIG_VELASENSE)

nuttx_add_application(
  NAME velasense
  STACKSIZE ${CONFIG_DEFAULT_TASK_STACKSIZE}
  PRIORITY 100
  SRCS main.c
)

endif()
