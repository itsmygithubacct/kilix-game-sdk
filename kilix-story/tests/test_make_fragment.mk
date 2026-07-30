STORY_TEST_ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/..)
include $(STORY_TEST_ROOT)/mk/kilix-story.mk
include $(STORY_TEST_ROOT)/tests/test_make_fragment_tail.mk

.PHONY: check-root

check-root:
	test "$(KILIX_STORY_ROOT)" = "$(STORY_TEST_ROOT)"
