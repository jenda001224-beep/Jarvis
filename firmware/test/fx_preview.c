#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fx.h"
#include "scene.h"

static void save(const char *path, const uint16_t *pixels)
{
    FILE *file = fopen(path, "wb");
    fprintf(file, "P6\n%d %d\n255\n", FX_WIDTH, FX_HEIGHT);
    for (int i = 0; i < FX_WIDTH * FX_HEIGHT; i++) {
        uint16_t p = pixels[i];
        uint8_t rgb[3] = {(uint8_t) (((p >> 11) & 31) * 255 / 31), (uint8_t) (((p >> 5) & 63) * 255 / 63),
                          (uint8_t) ((p & 31) * 255 / 31)};
        fwrite(rgb, 1, 3, file);
    }
    fclose(file);
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: fx_preview OUT_DIR\n");
        return 2;
    }
    static uint16_t pixels[FX_WIDTH * FX_HEIGHT];
    fx_init(malloc);
    scene_init();
    fx_frame_t frame = {0};
    const float step = 1.0f / 30.0f;
    struct {
        const char *name;
        scene_mode_t mode;
        bool hole;
        float seconds;
        float microphone;
        float speaker;
    } shots[] = {
        {"idle", SCENE_IDLE, true, 3.0f, 0.0f, 0.0f},
        {"wake", SCENE_LISTENING, false, 0.2f, 0.1f, 0.0f},
        {"listening_quiet", SCENE_LISTENING, false, 1.5f, 0.05f, 0.0f},
        {"listening_loud", SCENE_LISTENING, false, 0.5f, 0.9f, 0.0f},
        {"thinking", SCENE_THINKING, false, 1.3f, 0.0f, 0.0f},
        {"speaking", SCENE_SPEAKING, false, 1.0f, 0.0f, 0.7f},
        {"error", SCENE_ERROR, false, 1.2f, 0.0f, 0.0f},
        {"back", SCENE_IDLE, false, 0.25f, 0.0f, 0.0f},
        {"idle_again", SCENE_IDLE, true, 2.0f, 0.0f, 0.0f},
    };
    for (size_t s = 0; s < sizeof(shots) / sizeof(shots[0]); s++) {
        scene_set_mode(shots[s].mode);
        scene_set_hole(shots[s].hole);
        scene_set_levels(shots[s].microphone, shots[s].speaker);
        for (float t = 0.0f; t < shots[s].seconds; t += step) {
            scene_step(step, &frame);
            fx_render(&frame, pixels);
        }
        char path[512];
        snprintf(path, sizeof(path), "%s/%zu_%s.ppm", argv[1], s, shots[s].name);
        save(path, pixels);
        printf("%s: centre %.0f orb %.0f rings %.0f %.0f %.0f %.0f edge %.2f\n", shots[s].name, frame.center_y,
               frame.orb_radius, frame.ring[0].radius, frame.ring[1].radius, frame.ring[2].radius,
               frame.ring[3].radius, frame.edge);
    }
    return 0;
}
