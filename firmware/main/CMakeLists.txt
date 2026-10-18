idf_component_register(
    SRCS "main.c" "settings.c" "net.c" "audio.c" "resample.c" "wake.c" "proto.c" "live.c" "ikea.c" "ui.c" "lvgl_memory.c" "fx.c" "scene.c" "display.c"
         "agenda.c" "calendar.c" "bambu.c" "printer.c" "reminders.c" "geo.c" "travel.c" "web.c" "console.c" "meteo.c" "weather.c" "home.c" "control.c"
    INCLUDE_DIRS "."
    EMBED_FILES "fonts/inter_200.ttf" "fonts/inter_400.ttf" "fonts/inter_500.ttf" "fonts/inter_clock.ttf" "fonts/icons.ttf"
)

target_link_libraries(${COMPONENT_LIB} INTERFACE "-u lv_malloc_core")
