ifndef KILIX_UI_MK_INCLUDED
KILIX_UI_MK_INCLUDED := 1

KILIX_UI_DIR ?= third_party/kilix-ui
KILIX_UI_ROOT := $(abspath $(KILIX_UI_DIR))
KILIX_TOP_DOWN_DIR ?= third_party/kilix-top-down-engine
KILIX_TOP_DOWN_ROOT ?= $(abspath $(KILIX_TOP_DOWN_DIR))
ifneq ($(strip $(SOFT_RASTER_DIR)),)
KILIX_UI_SOFT_RASTER_ROOT := $(abspath $(SOFT_RASTER_DIR))
else
KILIX_UI_SOFT_RASTER_ROOT := \
	$(KILIX_TOP_DOWN_ROOT)/third_party/soft-raster
endif
KILIX_UI_BUILD_DIR ?= $(KILIX_UI_ROOT)/build
KILIX_UI_LIB := $(KILIX_UI_BUILD_DIR)/libkilix-ui.a
KILIX_UI_CPPFLAGS := -I$(KILIX_UI_ROOT)/include
KILIX_UI_TOP_DOWN_HEADERS := \
	$(wildcard $(KILIX_TOP_DOWN_ROOT)/include/*.h)
KILIX_UI_INPUTS := $(KILIX_UI_ROOT)/Makefile \
	$(wildcard $(KILIX_UI_ROOT)/include/*.h) \
	$(wildcard $(KILIX_UI_ROOT)/src/*.c) \
	$(KILIX_UI_TOP_DOWN_HEADERS) \
	$(KILIX_UI_SOFT_RASTER_ROOT)/include/soft_raster.h

$(KILIX_UI_LIB): $(KILIX_UI_INPUTS)
	$(MAKE) -C $(KILIX_UI_ROOT) \
		BUILD_DIR="$(KILIX_UI_BUILD_DIR)" \
		KILIX_TOP_DOWN_DIR="$(KILIX_TOP_DOWN_ROOT)" \
		SOFT_RASTER_DIR="$(KILIX_UI_SOFT_RASTER_ROOT)" all

endif
