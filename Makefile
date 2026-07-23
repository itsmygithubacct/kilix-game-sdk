PROJECT := kilix-world
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
LDLIBS += -lm

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
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

$(TOP_DOWN_OBJECT): src/kilix_world_top_down.c \
		include/kilix_world_top_down.h include/kilix_world.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

$(STATIC_LIB): $(OBJECT)
	$(AR) rcs $@ $^

$(TOP_DOWN_LIB): $(TOP_DOWN_OBJECT)
	$(AR) rcs $@ $^

$(SHARED_LIB): $(OBJECT)
	$(CC) -shared $(LDFLAGS) $^ -o $@

$(TEST): tests/test_world.c $(TOP_DOWN_LIB) $(STATIC_LIB) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(TOP_DOWN_LIB) $(STATIC_LIB) \
		$(LDFLAGS) $(LDLIBS) -o $@

test: $(TEST)
	$(TEST)

sanitize: | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) -std=c11 -O1 -g3 $(WARNINGS) \
		-fno-omit-frame-pointer -fsanitize=address,undefined \
		src/kilix_world.c src/kilix_world_top_down.c tests/test_world.c \
		-fsanitize=address,undefined $(LDLIBS) \
		-o $(BUILD_DIR)/test-world-sanitize
	ASAN_OPTIONS=detect_leaks=1 $(BUILD_DIR)/test-world-sanitize

test-clang:
	$(MAKE) clean
	$(MAKE) CC=clang CFLAGS="-O2 -g -Werror" test

install: all
	$(INSTALL) -d $(DESTDIR)$(PREFIX)/include $(DESTDIR)$(PREFIX)/lib
	$(INSTALL) -m 0644 include/kilix_world.h \
		include/kilix_world_top_down.h $(DESTDIR)$(PREFIX)/include/
	$(INSTALL) -m 0644 $(STATIC_LIB) $(TOP_DOWN_LIB) $(SHARED_LIB) \
		$(DESTDIR)$(PREFIX)/lib/

clean:
	rm -rf $(BUILD_DIR)

-include $(OBJECT:.o=.d) $(TOP_DOWN_OBJECT:.o=.d)
