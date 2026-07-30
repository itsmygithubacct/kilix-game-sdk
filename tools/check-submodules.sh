#!/bin/sh
set -eu

repository=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)

git -C "$repository" submodule status --recursive |
awk '
    substr($0, 1, 1) == " " { next }
    substr($0, 1, 1) == "-" {
        print "uninitialized submodule: " substr($0, 2) > "/dev/stderr"
        bad = 1
        next
    }
    substr($0, 1, 1) == "+" {
        print "unpinned submodule checkout: " substr($0, 2) > "/dev/stderr"
        bad = 1
        next
    }
    substr($0, 1, 1) == "U" {
        print "conflicted submodule: " substr($0, 2) > "/dev/stderr"
        bad = 1
        next
    }
    {
        print "unknown submodule state: " $0 > "/dev/stderr"
        bad = 1
    }
    END { exit bad }
'
