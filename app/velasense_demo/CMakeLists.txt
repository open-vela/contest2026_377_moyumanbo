if(CONFIG_VELASENSE_UI_DEMO)
  nuttx_add_application(
    NAME velasense
    SRCS main.c demo_ui.c fonts/lv_font_velasense_16.c
    STACKSIZE 32768
    PRIORITY 100
    DEPENDS lvgl)
endif()
