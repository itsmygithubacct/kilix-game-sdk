#include "kilix_game_kit.h"
#include "kilix_game_test.h"

#include <stdint.h>

int main(void)
{
    kilix_game_clock_options options;
    kilix_game_clock clock;

    if (KILIX_GAME_KIT_VERSION_MAJOR != 0 ||
        KILIX_GAME_KIT_VERSION_MINOR != 5 ||
        KILIX_GAME_KIT_VERSION_PATCH != 0)
        return 1;
    kilix_game_clock_options_init(&options);
    if (!kilix_game_clock_init(&clock, &options)) return 2;
    return kilix_test_hash64("kit", 3u) ==
           UINT64_C(0x3da04f1936381457) ? 0 : 3;
}
