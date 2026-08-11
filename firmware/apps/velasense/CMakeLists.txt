#
# VelaSense Application CMakeLists.txt
#

if(CONFIG_VELASENSE)

nuttx_add_application(
  NAME velasense
  STACKSIZE ${CONFIG_DEFAULT_TASK_STACKSIZE}
  PRIORITY 100
  SRCS
    main.c
    power_manager.c
    watchdog.c
    fault_handler.c
)

# Mimo Token Plan phone-side gateway components.
# These files implement the HTTPS client and privacy policy engine
# that run on the phone, not on the wearable.  They are compiled
# here for unit-test convenience; the production phone app links
# them from its own build system.

if(CONFIG_VELASENSE_MIMO_GATEWAY)

nuttx_add_library(mimo_gateway
  SRCS
    mimo_client.c
    mimo_templates.c
    privacy_policy.c
  INCLUDES
    ${CMAKE_CURRENT_SOURCE_DIR}
)

endif() # CONFIG_VELASENSE_MIMO_GATEWAY

endif() # CONFIG_VELASENSE
