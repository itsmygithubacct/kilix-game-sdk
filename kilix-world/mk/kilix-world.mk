ifndef KILIX_WORLD_ROOT
KILIX_WORLD_ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/..)
endif
KILIX_WORLD_BUILD_DIR ?= $(KILIX_WORLD_ROOT)/build
KILIX_WORLD_LIB := $(KILIX_WORLD_BUILD_DIR)/libkilix-world.a
KILIX_WORLD_TOP_DOWN_LIB := \
	$(KILIX_WORLD_BUILD_DIR)/libkilix-world-top-down.a
KILIX_WORLD_LIBS := $(KILIX_WORLD_TOP_DOWN_LIB) $(KILIX_WORLD_LIB)
KILIX_WORLD_CPPFLAGS := -I$(KILIX_WORLD_ROOT)/include
KILIX_WORLD_LDLIBS := -lm

$(KILIX_WORLD_LIB): \
		$(KILIX_WORLD_ROOT)/Makefile \
		$(KILIX_WORLD_ROOT)/include/kilix_world.h \
		$(KILIX_WORLD_ROOT)/src/kilix_world.c
	$(MAKE) -C $(KILIX_WORLD_ROOT) \
		BUILD_DIR=$(KILIX_WORLD_BUILD_DIR) $(KILIX_WORLD_LIB)

$(KILIX_WORLD_TOP_DOWN_LIB): \
		$(KILIX_WORLD_ROOT)/Makefile \
		$(KILIX_WORLD_ROOT)/include/kilix_world.h \
		$(KILIX_WORLD_ROOT)/include/kilix_world_top_down.h \
		$(KILIX_WORLD_ROOT)/src/kilix_world_top_down.c
	$(MAKE) -C $(KILIX_WORLD_ROOT) \
		BUILD_DIR=$(KILIX_WORLD_BUILD_DIR) $(KILIX_WORLD_TOP_DOWN_LIB)
