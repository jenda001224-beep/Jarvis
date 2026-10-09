idf_component_register(
    SRCS "main.c" "settings.c" "net.c" "audio.c" "resample.c" "wake.c" "proto.c" "live.c" "ikea.c" "ui.c"
    INCLUDE_DIRS "."
    EMBED_FILES "fonts/inter_200.ttf" "fonts/inter_400.ttf" "fonts/inter_500.ttf" "fonts/inter_clock.ttf" "fonts/icons.ttf"
)
