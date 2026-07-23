#!/bin/sh
set -eu

repository=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
top_down="$repository/third_party/kilix-top-down-engine"
soft_raster="$top_down/third_party/soft-raster"
temporary=$(mktemp -d "${TMPDIR:-/tmp}/kilix-ui-fragment.XXXXXX")
trap 'rm -rf "$temporary"' EXIT HUP INT TERM

{
    printf 'KILIX_UI_DIR := %s\n' "$repository"
    printf 'KILIX_UI_BUILD_DIR := %s/build\n' "$temporary"
    printf 'KILIX_TOP_DOWN_DIR := %s\n' "$top_down"
    printf 'SOFT_RASTER_DIR := %s\n' "$soft_raster"
    printf 'include %s/mk/kilix-ui.mk\n' "$repository"
    printf 'all: $(KILIX_UI_LIB)\n'
} >"$temporary/consumer.mk"

make -f "$temporary/consumer.mk" all >/dev/null
dependency_file="$temporary/build/kilix_ui.d"
test -f "$dependency_file"
grep -F "$top_down/include/kilix_top_down_types.h" \
    "$dependency_file" >/dev/null
grep -F "$soft_raster/include/soft_raster.h" \
    "$dependency_file" >/dev/null

printf '%s\n' \
    'PASS: consumer fragment forwards shared renderer and raster roots'
