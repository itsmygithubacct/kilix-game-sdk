#include "kilix_top_down.h"

#include <stdio.h>
#include <time.h>

static double now_ms(void)
{
    struct timespec now;
    timespec_get(&now, TIME_UTC);
    return (double)now.tv_sec * 1000.0 + (double)now.tv_nsec / 1000000.0;
}

int main(void)
{
    ki_td_soft_renderer renderer = {0};
    ki_td_fit_spec spec;
    ki_td_view view;
    if (!ki_td_soft_renderer_init(&renderer, 960, 540) ||
        !ki_td_fit_spec_init(&spec, 256, 176, 960, 540) ||
        !ki_td_view_fit(&view, &spec))
        return 1;
    double start = now_ms();
    for (int frame = 0; frame < 240; frame++) {
        ki_td_soft_clear(&renderer, 0x05060a);
        for (int y = 0; y < 11; y++)
            for (int x = 0; x < 16; x++)
                ki_td_soft_fill_rect(&renderer, &view, (float)(x * 16),
                                     (float)(y * 16), 16, 16,
                                     ((x + y + frame) & 1) ? 0x315b36 :
                                                              0x3d7042,
                                     1.0f);
        if (!ki_td_soft_pack_rgba(&renderer)) return 1;
    }
    double elapsed = now_ms() - start;
    printf("top-down benchmark: 960x540 240 frames %.3f ms/frame\n",
           elapsed / 240.0);
    ki_td_soft_renderer_destroy(&renderer);
    return 0;
}
