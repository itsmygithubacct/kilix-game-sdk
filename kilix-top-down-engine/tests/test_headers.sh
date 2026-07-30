#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
soft_raster=${SOFT_RASTER_DIR:-"$root/../kilix-game-kit/third_party/soft-raster"}
scratch=$(mktemp -d /tmp/kilix-top-down-headers.XXXXXX)
cleanup()
{
    rm -rf "$scratch"
}
trap cleanup EXIT HUP INT TERM

for header in kilix_top_down_types.h kilix_top_down_view.h \
              kilix_top_down_soft.h kilix_top_down.h; do
    printf '#include "%s"\nint main(void) { return 0; }\n' "$header" \
        >"$scratch/test.c"
    ${CC:-cc} -std=c11 -Werror -pedantic -I"$root/include" \
        -I"$soft_raster/include" -c "$scratch/test.c" \
        -o "$scratch/test.o"
    if command -v "${CXX:-c++}" >/dev/null 2>&1; then
        ${CXX:-c++} -std=c++17 -Werror -pedantic -I"$root/include" \
            -I"$soft_raster/include" -c "$scratch/test.c" \
            -o "$scratch/test-cxx.o"
    fi
done
printf 'PASS: public headers are independently includable from C and C++\n'
