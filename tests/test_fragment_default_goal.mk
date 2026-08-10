ifeq ($(strip $(FRAGMENT)),)
$(error FRAGMENT is required)
endif

include $(FRAGMENT)

.PHONY: consumer-default

consumer-default:
	test "$(.DEFAULT_GOAL)" = "consumer-default"
