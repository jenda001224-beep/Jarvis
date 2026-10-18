idf_component_register(
    SRCS "main.c" "settings.c" "net.c" "audio.c" "resample.c" "wake.c" "proto.c" "live.c" "ikea.c" "ui.c" "lvgl_memory.c" "fx.c" "scene.c" "display.c"
         "agenda.c" "calendar.c" "bambu.c" "printer.c" "reminders.c" "geo.c" "travel.c" "web.c" "console.c" "meteo.c" "weather.c" "home.c" "control.c" "tavily.c" "search.c" "todos.c" "alarms.c" "lists.c" "api.c"
    INCLUDE_DIRS "."
    EMBED_FILES "fonts/inter_200.ttf" "fonts/inter_400.ttf" "fonts/inter_500.ttf" "fonts/inter_clock.ttf" "fonts/icons.ttf"
                "sounds/ack_ano.pcm" "sounds/ack_posloucham.pcm" "sounds/ack_prosim.pcm" "sounds/alarm.pcm"
)

target_link_libraries(${COMPONENT_LIB} INTERFACE "-u lv_malloc_core")
