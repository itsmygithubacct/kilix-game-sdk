EXPECTED_GAME_KIT_ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/..)

include mk/game-kit.mk
include tests/test_make_fragment_tail.mk

.PHONY: check-root

check-root:
	test "$(KILIX_GAME_KIT_ROOT)" = "$(EXPECTED_GAME_KIT_ROOT)"
