PROJECT := kilix-story
BUILD_DIR ?= build
PREFIX ?= /usr/local
DESTDIR ?=

CC ?= cc
AR ?= ar
INSTALL ?= install

CPPFLAGS += -Iinclude
WARNINGS := -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
	-Wstrict-prototypes -Wmissing-prototypes -Wformat=2
CFLAGS ?= -O2 -g
override CFLAGS += -std=c11 -fPIC $(WARNINGS)

OBJECT := $(BUILD_DIR)/kilix_story.o
STATIC_LIB := $(BUILD_DIR)/lib$(PROJECT).a
SHARED_LIB := $(BUILD_DIR)/lib$(PROJECT).so
TEST := $(BUILD_DIR)/test-story

.PHONY: all clean install sanitize test test-clang test-make-fragment

all: $(STATIC_LIB) $(SHARED_LIB)

$(BUILD_DIR):
	mkdir -p $@

$(OBJECT): src/kilix_story.c include/kilix_story.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

$(STATIC_LIB): $(OBJECT)
	$(AR) rcs $@ $^

$(SHARED_LIB): $(OBJECT)
	$(CC) -shared $(LDFLAGS) $^ -o $@

$(TEST): tests/test_story.c $(STATIC_LIB) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(STATIC_LIB) $(LDFLAGS) -o $@

test: $(TEST) test-make-fragment
	$(TEST)

test-make-fragment:
	$(MAKE) -f tests/test_make_fragment.mk check-root

sanitize: | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) -std=c11 -O1 -g3 $(WARNINGS) \
		-fno-omit-frame-pointer -fsanitize=address,undefined \
		src/kilix_story.c tests/test_story.c \
		-fsanitize=address,undefined -o $(BUILD_DIR)/test-story-sanitize
	ASAN_OPTIONS=detect_leaks=1 $(BUILD_DIR)/test-story-sanitize

test-clang:
	$(MAKE) clean
	$(MAKE) CC=clang CFLAGS="-O2 -g -Werror" test

install: all
	$(INSTALL) -d $(DESTDIR)$(PREFIX)/include $(DESTDIR)$(PREFIX)/lib
	$(INSTALL) -m 0644 include/kilix_story.h $(DESTDIR)$(PREFIX)/include/
	$(INSTALL) -m 0644 $(STATIC_LIB) $(SHARED_LIB) \
		$(DESTDIR)$(PREFIX)/lib/

clean:
	rm -rf $(BUILD_DIR)

-include $(OBJECT:.o=.d)
