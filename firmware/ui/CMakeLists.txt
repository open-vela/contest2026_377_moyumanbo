#
# CMakeLists.txt – VelaSense wrist UI (LVGL 8.x)
#

# Collect all UI sources
set(UI_SRCS
    ui_theme.c
    ui_manager.c
    screen_home.c
    screen_event.c
    screen_trend.c
    screen_breath.c
    screen_settings.c
)

set(UI_HDRS
    ui_theme.h
    ui_manager.h
    screen_home.h
    screen_event.h
    screen_trend.h
    screen_breath.h
    screen_settings.h
)

# ── Build as a static library ────────────────────────────────────────
add_library(velasense_ui STATIC ${UI_SRCS} ${UI_HDRS})

# LVGL is expected to be available as a target or include path from the
# parent build.  If using LVGL as a subdirectory:
#   add_subdirectory(../../lib/lvgl lvgl)
# the target 'lvgl' is already defined.
#
# If LVGL headers live in a known path, use:
#   target_include_directories(velasense_ui PRIVATE ${LVGL_INCLUDE_DIR})

target_include_directories(velasense_ui
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)

# Link against LVGL if the target exists
if(TARGET lvgl)
    target_link_libraries(velasense_ui PUBLIC lvgl)
endif()

# Compiler warnings
target_compile_options(velasense_ui PRIVATE
    -Wall -Wextra -Wpedantic
    -Wno-unused-parameter
)
