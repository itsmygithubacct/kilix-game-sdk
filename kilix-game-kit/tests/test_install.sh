#!/bin/sh
set -eu

cc=${1:-cc}
cxx=${2:-c++}
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
temporary=$(mktemp -d "${TMPDIR:-/tmp}/kilix-game-kit-install.XXXXXX")
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
stage=$temporary/stage
build=$temporary/build

make -C "$root" --no-print-directory BUILD_DIR="$build" \
    DESTDIR="$stage" PREFIX=/usr install

for file in \
    kilix_game_kit.h kilix_game_loop.h kilix_game_runtime.h \
    kilix_game_audio.h kilix_game_test.h kitty_terminal_session.h \
    kitty_framebuffer.h kitty_input.h kitty_input_posix.h \
    kitty_keyboard.h kitty_keyboard_posix.h soft_raster.h pcm_mixer.h \
    pcmmix_bank.h kilix_state.h kilix_state_codec.h
do
    test -f "$stage/usr/include/$file"
done
test -f "$stage/usr/lib/libkilix-game-kit.a"
test -f "$stage/usr/lib/libkilix-game-test.a"

"$cc" -std=c11 -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE \
    -Wall -Wextra -Wpedantic -Werror \
    -I"$stage/usr/include" "$root/tests/install_c.c" \
    "$stage/usr/lib/libkilix-game-test.a" \
    "$stage/usr/lib/libkilix-game-kit.a" \
    -lz -lpthread -lm -lutil -o "$temporary/install-c"
"$temporary/install-c"

"$cxx" -std=c++17 -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE \
    -Wall -Wextra -Wpedantic -Werror \
    -I"$stage/usr/include" "$root/tests/install_cpp.cpp" \
    "$stage/usr/lib/libkilix-game-test.a" \
    "$stage/usr/lib/libkilix-game-kit.a" \
    -lz -lpthread -lm -lutil -o "$temporary/install-cpp"
"$temporary/install-cpp"
