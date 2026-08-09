PROJECT := kilix-world
BUILD_DIR ?= build
PREFIX ?= /usr/local
DESTDIR ?=

CC ?= cc
AR ?= ar
INSTALL ?= install

override CPPFLAGS += -Iinclude $(EXTRA_CPPFLAGS)
WARNINGS := -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
	-Wstrict-prototypes -Wmissing-prototypes -Wformat=2
CFLAGS ?= -O2 -g
override CFLAGS += -std=c11 -fPIC $(WARNINGS) $(EXTRA_CFLAGS)
override LDFLAGS += $(EXTRA_LDFLAGS)
override LDLIBS += -lm $(EXTRA_LDLIBS)
DEPFLAGS := -MMD -MP

OBJECT := $(BUILD_DIR)/kilix_world.o
TOP_DOWN_OBJECT := $(BUILD_DIR)/kilix_world_top_down.o
STATIC_LIB := $(BUILD_DIR)/lib$(PROJECT).a
TOP_DOWN_LIB := $(BUILD_DIR)/lib$(PROJECT)-top-down.a
SHARED_LIB := $(BUILD_DIR)/lib$(PROJECT).so
TEST := $(BUILD_DIR)/test-world

.PHONY: all clean install sanitize test test-clang

all: $(STATIC_LIB) $(TOP_DOWN_LIB) $(SHARED_LIB)

$(BUILD_DIR):
	mkdir -p $@

$(OBJECT): src/kilix_world.c include/kilix_world.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

$(TOP_DOWN_OBJECT): src/kilix_world_top_down.c \
		include/kilix_world_top_down.h include/kilix_world.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

$(STATIC_LIB): $(OBJECT)
	$(AR) rcs $@ $^

$(TOP_DOWN_LIB): $(TOP_DOWN_OBJECT)
	$(AR) rcs $@ $^

$(SHARED_LIB): $(OBJECT)
	$(CC) -shared $(LDFLAGS) $^ $(LDLIBS) -o $@

$(TEST): tests/test_world.c $(TOP_DOWN_LIB) $(STATIC_LIB) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) $< \
		$(TOP_DOWN_LIB) $(STATIC_LIB) \
		$(LDFLAGS) $(LDLIBS) -o $@

test: $(TEST)
	$(TEST)

SANITIZE_BUILD_DIR ?= $(BUILD_DIR)/sanitize
sanitize:
	@ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
		UBSAN_OPTIONS=halt_on_error=1 \
		$(MAKE) --no-print-directory BUILD_DIR="$(SANITIZE_BUILD_DIR)" \
		EXTRA_CFLAGS="-O1 -g3 -fno-omit-frame-pointer \
		-fsanitize=address,undefined" \
		EXTRA_LDFLAGS="-fsanitize=address,undefined" test

CLANG_BUILD_DIR ?= $(BUILD_DIR)/clang
test-clang:
	$(MAKE) --no-print-directory BUILD_DIR="$(CLANG_BUILD_DIR)" clean
	$(MAKE) --no-print-directory CC=clang \
		BUILD_DIR="$(CLANG_BUILD_DIR)" EXTRA_CFLAGS="-Werror" test

install: all
	$(INSTALL) -d $(DESTDIR)$(PREFIX)/include $(DESTDIR)$(PREFIX)/lib
	$(INSTALL) -m 0644 include/kilix_world.h \
		include/kilix_world_top_down.h $(DESTDIR)$(PREFIX)/include/
	$(INSTALL) -m 0644 $(STATIC_LIB) $(TOP_DOWN_LIB) $(SHARED_LIB) \
		$(DESTDIR)$(PREFIX)/lib/

clean:
	rm -rf $(BUILD_DIR)

-include $(OBJECT:.o=.d) $(TOP_DOWN_OBJECT:.o=.d) $(TEST).d
