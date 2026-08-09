PROJECT := kilix-ui
BUILD_DIR ?= build
PREFIX ?= /usr/local
DESTDIR ?=
KILIX_TOP_DOWN_DIR ?= ../kilix-top-down-engine
SOFT_RASTER_DIR ?= ../kilix-game-kit/third_party/soft-raster
KILIX_TOP_DOWN_BUILD_DIR ?= $(abspath $(KILIX_TOP_DOWN_DIR)/build)
SOFT_RASTER_BUILD_DIR ?= $(abspath $(SOFT_RASTER_DIR)/build)

CC ?= cc
CXX ?= c++
CLANG ?= clang
CLANGXX ?= clang++
AR ?= ar
ARFLAGS := rcs
INSTALL ?= install

EXTRA_CPPFLAGS ?=
EXTRA_CFLAGS ?=
EXTRA_CXXFLAGS ?=
EXTRA_LDFLAGS ?=
EXTRA_LDLIBS ?=

INCLUDES := -Iinclude -I$(KILIX_TOP_DOWN_DIR)/include \
	-I$(SOFT_RASTER_DIR)/include
override CPPFLAGS += $(INCLUDES) $(EXTRA_CPPFLAGS)
WARNINGS := -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
	-Wstrict-prototypes -Wmissing-prototypes -Wformat=2
CFLAGS ?= -O2 -g
override CFLAGS += -std=c11 -fPIC $(WARNINGS) $(EXTRA_CFLAGS)
CXXFLAGS ?= -O2 -g
override CXXFLAGS += -std=c++17 -Wall -Wextra -Wpedantic -Wconversion \
	-Wshadow -Wformat=2 $(EXTRA_CXXFLAGS)
override LDFLAGS += $(EXTRA_LDFLAGS)
override LDLIBS += -lm $(EXTRA_LDLIBS)

OBJECT := $(BUILD_DIR)/kilix_ui.o
LIB := $(BUILD_DIR)/lib$(PROJECT).a
TEST_OBJECT := $(BUILD_DIR)/test_ui.o
TEST := $(BUILD_DIR)/test-ui
NOALLOC_OBJECT := $(BUILD_DIR)/test_noalloc.o
NOALLOC_TEST := $(BUILD_DIR)/test-ui-noalloc
CPP_TEST_OBJECT := $(BUILD_DIR)/test_cpp.o
BENCHMARK_OBJECT := $(BUILD_DIR)/benchmark_ui.o
BENCHMARK := $(BUILD_DIR)/benchmark-ui
SANITIZE_BUILD_DIR ?= $(BUILD_DIR)/sanitize
SANITIZE_TEST := $(SANITIZE_BUILD_DIR)/test-ui-sanitize
CLANG_BUILD_DIR ?= $(BUILD_DIR)/clang

KILIX_TD_SOFT_LIB := $(KILIX_TOP_DOWN_BUILD_DIR)/libkilix-top-down-soft.a
KILIX_TD_CORE_LIB := $(KILIX_TOP_DOWN_BUILD_DIR)/libkilix-top-down-core.a
SOFT_RASTER_LIB := $(SOFT_RASTER_BUILD_DIR)/libsoft-raster.a
DEPENDENCY_LIBS := $(KILIX_TD_SOFT_LIB) $(KILIX_TD_CORE_LIB) \
	$(SOFT_RASTER_LIB)

.PHONY: all analyze benchmark clean install sanitize test test-clang \
	test-cpp test-fragment

all: $(LIB)

$(BUILD_DIR) $(SANITIZE_BUILD_DIR):
	mkdir -p $@

$(OBJECT): src/kilix_ui.c include/kilix_ui.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

$(LIB): $(OBJECT)
	$(AR) $(ARFLAGS) $@ $^

$(SOFT_RASTER_LIB):
	$(MAKE) -C $(SOFT_RASTER_DIR) BUILD_DIR=$(SOFT_RASTER_BUILD_DIR) $@

$(KILIX_TD_SOFT_LIB) $(KILIX_TD_CORE_LIB) &: $(SOFT_RASTER_LIB)
	$(MAKE) -C $(KILIX_TOP_DOWN_DIR) \
		BUILD_DIR=$(KILIX_TOP_DOWN_BUILD_DIR) \
		SOFT_RASTER_DIR=$(abspath $(SOFT_RASTER_DIR)) \
		SOFT_RASTER_BUILD_DIR=$(SOFT_RASTER_BUILD_DIR) \
		$(KILIX_TD_SOFT_LIB) $(KILIX_TD_CORE_LIB)

$(TEST_OBJECT): tests/test_ui.c include/kilix_ui.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

$(TEST): $(TEST_OBJECT) $(LIB) $(DEPENDENCY_LIBS)
	$(CC) $(LDFLAGS) $(TEST_OBJECT) $(LIB) $(DEPENDENCY_LIBS) \
		$(LDLIBS) -o $@

$(NOALLOC_OBJECT): tests/test_noalloc.c include/kilix_ui.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

$(NOALLOC_TEST): $(NOALLOC_OBJECT) $(LIB) $(DEPENDENCY_LIBS)
	$(CC) $(LDFLAGS) $(NOALLOC_OBJECT) $(LIB) $(DEPENDENCY_LIBS) \
		-Wl,--wrap=malloc -Wl,--wrap=calloc -Wl,--wrap=realloc \
		-Wl,--wrap=free $(LDLIBS) -o $@

$(CPP_TEST_OBJECT): tests/test_cpp.cpp include/kilix_ui.h | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c $< -o $@

test-cpp: $(CPP_TEST_OBJECT)

test: $(TEST) $(NOALLOC_TEST) test-cpp test-fragment
	$(TEST)
	$(NOALLOC_TEST)

test-fragment:
	KILIX_TOP_DOWN_DIR=$(abspath $(KILIX_TOP_DOWN_DIR)) \
	SOFT_RASTER_DIR=$(abspath $(SOFT_RASTER_DIR)) \
		tests/test_make_fragment.sh

test-clang:
	$(MAKE) CC=$(CLANG) CXX=$(CLANGXX) BUILD_DIR=$(CLANG_BUILD_DIR) test

$(BENCHMARK_OBJECT): bench/benchmark_ui.c include/kilix_ui.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -DNDEBUG -MMD -MP -c $< -o $@

$(BENCHMARK): $(BENCHMARK_OBJECT) $(LIB) $(DEPENDENCY_LIBS)
	$(CC) $(LDFLAGS) $(BENCHMARK_OBJECT) $(LIB) $(DEPENDENCY_LIBS) \
		$(LDLIBS) -o $@

benchmark: $(BENCHMARK)
	$(BENCHMARK)

sanitize: $(DEPENDENCY_LIBS) | $(SANITIZE_BUILD_DIR)
	$(CC) $(CPPFLAGS) -std=c11 -O1 -g3 $(WARNINGS) \
		-fno-omit-frame-pointer -fsanitize=address,undefined \
		src/kilix_ui.c tests/test_ui.c $(DEPENDENCY_LIBS) \
		$(LDFLAGS) $(LDLIBS) -fsanitize=address,undefined \
		-o $(SANITIZE_TEST)
	ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 \
		$(SANITIZE_TEST)

analyze:
	$(CC) $(CPPFLAGS) $(CFLAGS) -fanalyzer -fsyntax-only src/kilix_ui.c

install: $(LIB)
	$(INSTALL) -d $(DESTDIR)$(PREFIX)/include $(DESTDIR)$(PREFIX)/lib
	$(INSTALL) -m 0644 include/kilix_ui.h $(DESTDIR)$(PREFIX)/include/
	$(INSTALL) -m 0644 $(LIB) $(DESTDIR)$(PREFIX)/lib/

clean:
	rm -rf $(BUILD_DIR)

-include $(OBJECT:.o=.d) $(TEST_OBJECT:.o=.d) $(NOALLOC_OBJECT:.o=.d) \
	$(CPP_TEST_OBJECT:.o=.d) $(BENCHMARK_OBJECT:.o=.d)
