#include "kilix_top_down.h"

#include <stdio.h>

int main(int argc, char **argv)
{
    static const uint8_t hero_pixels[] = {
          0,   0,   0,   0, 255, 209, 102, 255, 255, 209, 102, 255,   0,   0,   0,   0,
        116, 192, 252, 255, 248, 249, 250, 255, 248, 249, 250, 255, 116, 192, 252, 255,
          0,   0,   0,   0, 105, 219, 124, 255, 105, 219, 124, 255,   0,   0,   0,   0
    };
    const char *path = argc > 1 ? argv[1] : "headless-viewer.ppm";
    ki_td_soft_renderer renderer = {0};
    ki_td_fit_spec spec;
    ki_td_view view;
    ki_td_rgba8 hero = ki_td_rgba8_make(hero_pixels, 4, 3);
    if (!ki_td_soft_renderer_init(&renderer, 320, 240) ||
        !ki_td_fit_spec_init(&spec, 64, 48, 320, 240)) {
        fprintf(stderr, "could not initialize renderer\n");
        return 1;
    }
    spec.scale_policy = KI_TD_SCALE_PIXEL_ART;
    spec.minimum_scale = 1.0f;
    if (!ki_td_view_fit(&view, &spec)) return 1;

    ki_td_soft_clear(&renderer, 0x05060a);
    ki_td_soft_fill_rect(&renderer, &view, 2, 2, 60, 44, 0x214d32, 1.0f);
    for (int y = 4; y < 44; y += 8)
        for (int x = 4; x < 60; x += 8)
            ki_td_soft_fill_rect(&renderer, &view, (float)x, (float)y, 6, 6,
                                 ((x + y) / 8) % 2 ? 0x2f7d46 : 0x3f9655,
                                 1.0f);
    ki_td_soft_fill_ellipse(&renderer, &view, 32, 36, 8, 2.5f,
                            0x030405, 0.35f);
    ki_td_soft_rgba_pixel_art(&renderer, &view, 30, 30, &hero, 1.0f);
    ki_td_soft_line(&renderer, &view, 8, 12, 55, 12, 1.0f,
                    0xffd166, 0.9f);
    sr_text_shadow(ki_td_soft_canvas(&renderer), 65, 18,
                   "KILIX TOP DOWN", 0xffd166, 1.0f, 2);
    sr_text_shadow(ki_td_soft_canvas(&renderer), 84, 205,
                   "ORTHOGRAPHIC / PIXEL ART", 0xd8f3dc, 1.0f, 1);

    if (!ki_td_soft_pack_rgba(&renderer) ||
        !sr_write_ppm(ki_td_soft_canvas_const(&renderer), path)) {
        fprintf(stderr, "could not write %s\n", path);
        ki_td_soft_renderer_destroy(&renderer);
        return 1;
    }
    printf("wrote %s\n", path);
    ki_td_soft_renderer_destroy(&renderer);
    return 0;
}
