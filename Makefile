PROJECT := kilix-ui
BUILD_DIR ?= build
PREFIX ?= /usr/local
DESTDIR ?=
KILIX_TOP_DOWN_DIR ?= third_party/kilix-top-down-engine
SOFT_RASTER_DIR ?= $(KILIX_TOP_DOWN_DIR)/third_party/soft-raster

CC ?= cc
AR ?= ar
INSTALL ?= install

CPPFLAGS += -Iinclude -I$(KILIX_TOP_DOWN_DIR)/include \
	-I$(SOFT_RASTER_DIR)/include
WARNINGS := -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
	-Wstrict-prototypes -Wmissing-prototypes -Wformat=2
CFLAGS ?= -O2 -g
override CFLAGS += -std=c11 -fPIC $(WARNINGS)
LDLIBS := -lm

OBJECT := $(BUILD_DIR)/kilix_ui.o
LIB := $(BUILD_DIR)/lib$(PROJECT).a
TEST := $(BUILD_DIR)/test-ui
NOALLOC_TEST := $(BUILD_DIR)/test-ui-noalloc
KILIX_TD_SOFT_LIB := $(KILIX_TOP_DOWN_DIR)/build/libkilix-top-down-soft.a
KILIX_TD_CORE_LIB := $(KILIX_TOP_DOWN_DIR)/build/libkilix-top-down-core.a
SOFT_RASTER_LIB := $(SOFT_RASTER_DIR)/build/libsoft-raster.a

.PHONY: all clean install sanitize test

all: $(LIB)

$(BUILD_DIR):
	mkdir -p $@

$(OBJECT): src/kilix_ui.c include/kilix_ui.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

$(LIB): $(OBJECT)
	$(AR) rcs $@ $^

$(KILIX_TD_SOFT_LIB) $(KILIX_TD_CORE_LIB) $(SOFT_RASTER_LIB):
	$(MAKE) -C $(KILIX_TOP_DOWN_DIR) all

$(TEST): tests/test_ui.c $(LIB) $(KILIX_TD_SOFT_LIB) \
	$(KILIX_TD_CORE_LIB) $(SOFT_RASTER_LIB) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(LIB) $(KILIX_TD_SOFT_LIB) \
		$(KILIX_TD_CORE_LIB) $(SOFT_RASTER_LIB) $(LDLIBS) -o $@

$(NOALLOC_TEST): tests/test_noalloc.c $(LIB) $(KILIX_TD_SOFT_LIB) \
	$(KILIX_TD_CORE_LIB) $(SOFT_RASTER_LIB) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(LIB) $(KILIX_TD_SOFT_LIB) \
		$(KILIX_TD_CORE_LIB) $(SOFT_RASTER_LIB) $(LDLIBS) \
		-Wl,--wrap=malloc -Wl,--wrap=calloc -Wl,--wrap=realloc \
		-Wl,--wrap=free -o $@

test: $(TEST) $(NOALLOC_TEST)
	$(TEST)
	$(NOALLOC_TEST)

sanitize: $(KILIX_TD_SOFT_LIB) $(KILIX_TD_CORE_LIB) $(SOFT_RASTER_LIB) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) -std=c11 -O1 -g3 $(WARNINGS) \
		-fno-omit-frame-pointer -fsanitize=address,undefined \
		src/kilix_ui.c tests/test_ui.c $(KILIX_TD_SOFT_LIB) \
		$(KILIX_TD_CORE_LIB) $(SOFT_RASTER_LIB) $(LDLIBS) \
		-fsanitize=address,undefined -o $(BUILD_DIR)/test-ui-sanitize
	ASAN_OPTIONS=detect_leaks=1 $(BUILD_DIR)/test-ui-sanitize

install: $(LIB)
	$(INSTALL) -d $(DESTDIR)$(PREFIX)/include $(DESTDIR)$(PREFIX)/lib
	$(INSTALL) -m 0644 include/kilix_ui.h $(DESTDIR)$(PREFIX)/include/
	$(INSTALL) -m 0644 $(LIB) $(DESTDIR)$(PREFIX)/lib/

clean:
	rm -rf $(BUILD_DIR)

-include $(OBJECT:.o=.d)
