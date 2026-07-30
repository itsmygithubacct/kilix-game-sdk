#!/bin/sh
set -eu

repository=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
failed=0

git -C "$repository" submodule status --recursive |
while IFS= read -r line; do
    marker=$(printf '%s' "$line" | cut -c1)
    case "$marker" in
        ' ') ;;
        '-') printf '%s\n' "uninitialized submodule: ${line#?}" >&2; failed=1 ;;
        '+') printf '%s\n' "unpinned submodule checkout: ${line#?}" >&2; failed=1 ;;
        'U') printf '%s\n' "conflicted submodule: ${line#?}" >&2; failed=1 ;;
        *) printf '%s\n' "unknown submodule state: $line" >&2; failed=1 ;;
    esac
done

# The loop may run in a subshell, so perform a second predicate that carries
# its exit status portably.
git -C "$repository" submodule status --recursive |
awk 'substr($0,1,1) != " " { bad=1 } END { exit bad }'
