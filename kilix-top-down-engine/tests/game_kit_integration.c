#include "kilix_game_kit.h"
#include "kilix_top_down.h"

#include <stdio.h>

int main(void)
{
    ki_td_soft_renderer renderer = {0};
    ki_td_fit_spec spec;
    ki_td_view view;
    if (!ki_td_soft_renderer_init(&renderer, 64, 48) ||
        !ki_td_fit_spec_init(&spec, 16, 12, 64, 48) ||
        !ki_td_view_fit(&view, &spec))
        return 1;
    ki_td_soft_clear(&renderer, sr_rgb(3, 5, 8));
    ki_td_soft_fill_rect(&renderer, &view, 2, 2, 12, 8,
                         sr_rgb(116, 192, 252), 1.0f);
    if (!ki_td_soft_pack_rgba(&renderer)) return 1;
    ki_td_soft_renderer_destroy(&renderer);
    printf("PASS: top-down engine shares game-kit's soft-raster\n");
    return 0;
}
