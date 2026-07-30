#!/bin/sh
set -eu

status=$(git submodule status --recursive)
if test -z "$status"; then
    echo "no pinned submodules found" >&2
    exit 1
fi
if printf '%s\n' "$status" | grep -E '^[+U-]' >/dev/null; then
    printf '%s\n' "$status" >&2
    echo "submodule checkout does not match the recorded pin" >&2
    exit 1
fi
if ! git -C third_party/soft-raster diff --quiet --ignore-submodules --; then
    echo "soft-raster submodule is dirty" >&2
    exit 1
fi
printf 'PASS: dependency pins match\n'
