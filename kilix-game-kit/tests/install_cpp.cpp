#include "kilix_game_kit.h"
#include "kilix_game_test.h"

#include <cstdint>

int main()
{
    kilix_game_clock_options options{};
    kilix_game_clock clock{};

    kilix_game_clock_options_init(&options);
    if (!kilix_game_clock_init(&clock, &options)) return 1;
    return kilix_test_hash64("kit", 3u) ==
           UINT64_C(0x3da04f1936381457) ? 0 : 2;
}
