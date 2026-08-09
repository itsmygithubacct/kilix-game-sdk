PROJECT := kilix-story
BUILD_DIR ?= build
PREFIX ?= /usr/local
DESTDIR ?=

CC ?= cc
AR ?= ar
INSTALL ?= install

override CPPFLAGS += -Iinclude
WARNINGS := -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
	-Wstrict-prototypes -Wmissing-prototypes -Wformat=2
CFLAGS ?= -O2 -g
override CFLAGS += -std=c11 -fPIC $(WARNINGS) $(EXTRA_CFLAGS)
override LDFLAGS += $(EXTRA_LDFLAGS)
override LDLIBS += $(EXTRA_LDLIBS)
DEPFLAGS := -MMD -MP

OBJECT := $(BUILD_DIR)/kilix_story.o
STATIC_LIB := $(BUILD_DIR)/lib$(PROJECT).a
SHARED_LIB := $(BUILD_DIR)/lib$(PROJECT).so
TEST := $(BUILD_DIR)/test-story

.PHONY: all clean install sanitize test test-clang test-make-fragment

all: $(STATIC_LIB) $(SHARED_LIB)

$(BUILD_DIR):
	mkdir -p $@

$(OBJECT): src/kilix_story.c include/kilix_story.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

$(STATIC_LIB): $(OBJECT)
	$(AR) rcs $@ $^

$(SHARED_LIB): $(OBJECT)
	$(CC) -shared $(LDFLAGS) $^ $(LDLIBS) -o $@

$(TEST): tests/test_story.c $(STATIC_LIB) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) $< $(STATIC_LIB) \
		$(LDFLAGS) $(LDLIBS) -o $@

test: $(TEST) test-make-fragment
	$(TEST)

test-make-fragment:
	$(MAKE) -f tests/test_make_fragment.mk check-root

SANITIZE_BUILD_DIR ?= $(BUILD_DIR)/sanitize
sanitize:
	@ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
		UBSAN_OPTIONS=halt_on_error=1 \
		$(MAKE) --no-print-directory BUILD_DIR="$(SANITIZE_BUILD_DIR)" \
		EXTRA_CFLAGS="-O1 -fno-omit-frame-pointer \
		-fsanitize=address,undefined" \
		EXTRA_LDFLAGS="-fsanitize=address,undefined" test

CLANG_BUILD_DIR ?= $(BUILD_DIR)/clang
test-clang:
	$(MAKE) --no-print-directory BUILD_DIR="$(CLANG_BUILD_DIR)" clean
	$(MAKE) --no-print-directory CC=clang \
		BUILD_DIR="$(CLANG_BUILD_DIR)" EXTRA_CFLAGS="-Werror" test

install: all
	$(INSTALL) -d $(DESTDIR)$(PREFIX)/include $(DESTDIR)$(PREFIX)/lib
	$(INSTALL) -m 0644 include/kilix_story.h $(DESTDIR)$(PREFIX)/include/
	$(INSTALL) -m 0644 $(STATIC_LIB) $(SHARED_LIB) \
		$(DESTDIR)$(PREFIX)/lib/

clean:
	rm -rf $(BUILD_DIR)

-include $(OBJECT:.o=.d) $(TEST).d
