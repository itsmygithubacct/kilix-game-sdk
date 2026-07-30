#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
scratch=$(mktemp -d /tmp/kilix-top-down-install.XXXXXX)
cleanup()
{
    rm -rf "$scratch"
}
trap cleanup EXIT HUP INT TERM

prefix=/opt/kilix
make -C "$root" DESTDIR="$scratch" PREFIX="$prefix" install >/dev/null
make -C "$root/third_party/soft-raster" DESTDIR="$scratch" \
    PREFIX="$prefix" install >/dev/null

cat >"$scratch/consumer.c" <<'EOF'
#include <kilix_top_down.h>
int main(void)
{
    ki_td_soft_renderer renderer = {0};
    if (!ki_td_soft_renderer_init(&renderer, 8, 8)) return 1;
    ki_td_soft_clear(&renderer, 0x123456);
    if (!ki_td_soft_pack_rgba(&renderer)) return 1;
    ki_td_soft_renderer_destroy(&renderer);
    return 0;
}
EOF

flags=$(PKG_CONFIG_PATH="$scratch$prefix/lib/pkgconfig" \
    PKG_CONFIG_SYSROOT_DIR="$scratch" \
    pkg-config --cflags --libs --static kilix-top-down-engine)
${CC:-cc} -std=c11 "$scratch/consumer.c" $flags -o "$scratch/consumer"
LD_LIBRARY_PATH="$scratch$prefix/lib" "$scratch/consumer"
printf 'PASS: staged pkg-config installation builds and runs a consumer\n'
