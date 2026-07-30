PROJECT := kilix-top-down
BUILD_DIR ?= build
PREFIX ?= /usr/local
DESTDIR ?=
SOFT_RASTER_DIR ?= third_party/soft-raster
KILIX_GAME_KIT_ROOT ?= ../kilix-game-kit

CC ?= cc
AR ?= ar
NM ?= nm
INSTALL ?= install

CPPFLAGS += -Iinclude -I$(SOFT_RASTER_DIR)/include
WARNINGS := \
	-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow \
	-Wstrict-prototypes -Wmissing-prototypes -Wformat=2
CFLAGS ?= -O2 -g
override CFLAGS += -std=c11 -fPIC $(WARNINGS) -MMD -MP
LDLIBS := -lm

CORE_SOURCES := src/view.c
SOFT_SOURCES := src/soft.c
CORE_OBJECTS := $(CORE_SOURCES:src/%.c=$(BUILD_DIR)/obj/core/%.o)
SOFT_OBJECTS := $(SOFT_SOURCES:src/%.c=$(BUILD_DIR)/obj/soft/%.o)
CORE_LIB := $(BUILD_DIR)/lib$(PROJECT)-core.a
SOFT_LIB := $(BUILD_DIR)/lib$(PROJECT)-soft.a
LIBS := $(SOFT_LIB) $(CORE_LIB)
SOFT_RASTER_LIB := $(SOFT_RASTER_DIR)/build/libsoft-raster.a

PUBLIC_HEADERS := \
	include/kilix_top_down.h \
	include/kilix_top_down_types.h \
	include/kilix_top_down_view.h \
	include/kilix_top_down_soft.h
CORE_PUBLIC_HEADERS := include/kilix_top_down_types.h \
	include/kilix_top_down_view.h

TEST_BIN := $(BUILD_DIR)/test-kilix-top-down
NOALLOC_BIN := $(BUILD_DIR)/test-noalloc
EXAMPLE_BIN := $(BUILD_DIR)/headless-viewer
BENCH_BIN := $(BUILD_DIR)/benchmark
INTEGRATION_BIN := $(BUILD_DIR)/game-kit-integration
PKG_CONFIG_FILE := $(BUILD_DIR)/kilix-top-down-engine.pc

.PHONY: all clean install test test-c test-noalloc test-deps test-integration \
	test-headers test-install test-clang sanitize golden benchmark \
	release-gate FORCE

all: $(CORE_LIB) $(SOFT_LIB) $(EXAMPLE_BIN)

$(BUILD_DIR)/obj/core $(BUILD_DIR)/obj/soft:
	mkdir -p $@

$(BUILD_DIR)/obj/core/%.o: src/%.c $(CORE_PUBLIC_HEADERS) Makefile | $(BUILD_DIR)/obj/core
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/obj/soft/%.o: src/%.c $(PUBLIC_HEADERS) Makefile | $(BUILD_DIR)/obj/soft
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(CORE_LIB): $(CORE_OBJECTS)
	$(AR) rcs $@ $^

$(SOFT_LIB): $(SOFT_OBJECTS)
	$(AR) rcs $@ $^

$(SOFT_RASTER_LIB):
	$(MAKE) -C $(SOFT_RASTER_DIR) build/libsoft-raster.a

$(TEST_BIN): tests/test_top_down.c $(LIBS) $(SOFT_RASTER_LIB)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(LIBS) $(SOFT_RASTER_LIB) \
		$(LDFLAGS) $(LDLIBS) -o $@

$(NOALLOC_BIN): tests/test_noalloc.c $(LIBS) $(SOFT_RASTER_LIB)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(LIBS) $(SOFT_RASTER_LIB) \
		$(LDFLAGS) $(LDLIBS) -Wl,--wrap=malloc -Wl,--wrap=calloc \
		-Wl,--wrap=realloc -Wl,--wrap=free -o $@

$(EXAMPLE_BIN): examples/headless_viewer.c $(LIBS) $(SOFT_RASTER_LIB)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(LIBS) $(SOFT_RASTER_LIB) \
		$(LDFLAGS) $(LDLIBS) -o $@

$(BENCH_BIN): examples/benchmark.c $(LIBS) $(SOFT_RASTER_LIB)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(LIBS) $(SOFT_RASTER_LIB) \
		$(LDFLAGS) $(LDLIBS) -o $@

test-c: $(TEST_BIN)
	$(TEST_BIN)

test-noalloc: $(NOALLOC_BIN)
	$(NOALLOC_BIN)

test: test-c test-noalloc golden

test-deps:
	$(MAKE) -C $(SOFT_RASTER_DIR) test
	tools/check-submodules.sh

ifneq ($(wildcard $(KILIX_GAME_KIT_ROOT)/mk/game-kit.mk),)
include $(KILIX_GAME_KIT_ROOT)/mk/game-kit.mk

$(INTEGRATION_BIN): tests/game_kit_integration.c $(LIBS) $(KILIX_GAME_KIT_LIB)
	$(CC) $(CPPFLAGS) $(KILIX_GAME_KIT_CPPFLAGS) \
		-D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE $(CFLAGS) $< \
		$(LIBS) $(KILIX_GAME_KIT_LIB) $(KILIX_GAME_KIT_LDLIBS) -o $@

test-integration: $(INTEGRATION_BIN)
	$(INTEGRATION_BIN)
	@test "$$($(NM) -g $(INTEGRATION_BIN) | \
		awk '$$3 == "sr_canvas_init" { count += 1 } END { print count + 0 }')" \
		-eq 1
	@test "$$($(NM) -g $(INTEGRATION_BIN) | \
		awk '$$3 == "ki_td_view_fit" { count += 1 } END { print count + 0 }')" \
		-eq 1
else
test-integration:
	@echo "kilix-game-kit not found at $(KILIX_GAME_KIT_ROOT)" >&2
	@exit 1
endif

test-headers:
	tests/test_headers.sh

$(PKG_CONFIG_FILE): kilix-top-down-engine.pc.in Makefile FORCE
	mkdir -p $(@D)
	sed 's|@PREFIX@|$(PREFIX)|g' $< >$@

FORCE:

install: $(LIBS) $(PKG_CONFIG_FILE)
	$(INSTALL) -d $(DESTDIR)$(PREFIX)/include \
		$(DESTDIR)$(PREFIX)/lib/pkgconfig
	$(INSTALL) -m 0644 $(PUBLIC_HEADERS) $(DESTDIR)$(PREFIX)/include/
	$(INSTALL) -m 0644 $(LIBS) $(DESTDIR)$(PREFIX)/lib/
	$(INSTALL) -m 0644 $(PKG_CONFIG_FILE) \
		$(DESTDIR)$(PREFIX)/lib/pkgconfig/kilix-top-down-engine.pc

test-install: $(LIBS) $(PKG_CONFIG_FILE)
	tests/test_install.sh

sanitize:
	mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) -std=c11 -O1 -g3 $(WARNINGS) \
		-fno-omit-frame-pointer -fsanitize=address,undefined \
		$(CORE_SOURCES) $(SOFT_SOURCES) tests/test_top_down.c \
		$(SOFT_RASTER_DIR)/src/soft_raster.c \
		-fsanitize=address,undefined $(LDLIBS) \
		-o $(BUILD_DIR)/test-kilix-top-down-sanitize
	ASAN_OPTIONS=detect_leaks=1 $(BUILD_DIR)/test-kilix-top-down-sanitize

test-clang:
	@command -v clang >/dev/null || { \
		echo "clang is required for test-clang" >&2; exit 1; \
	}
	$(MAKE) BUILD_DIR=$(BUILD_DIR)/clang CC=clang CFLAGS='-O2 -g -Werror' \
		test-c test-noalloc

golden: $(EXAMPLE_BIN)
	mkdir -p $(BUILD_DIR)/golden
	$(EXAMPLE_BIN) $(BUILD_DIR)/golden/headless-viewer.ppm
	cd $(BUILD_DIR)/golden && \
		sha256sum -c $(abspath tests/golden/headless-viewer.sha256)

benchmark: $(BENCH_BIN)
	$(BENCH_BIN)

release-gate: all test sanitize test-deps test-integration test-headers \
	test-install test-clang benchmark

clean:
	rm -rf $(BUILD_DIR)

-include $(CORE_OBJECTS:.o=.d) $(SOFT_OBJECTS:.o=.d)
