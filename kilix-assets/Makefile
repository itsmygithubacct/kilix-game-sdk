PROJECT := kilix-assets
BUILD_DIR ?= build
PREFIX ?= /usr/local
DESTDIR ?=

CC ?= cc
AR ?= ar
INSTALL ?= install

CPPFLAGS += -D_POSIX_C_SOURCE=200809L -Iinclude
WARNINGS := -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
	-Wstrict-prototypes -Wmissing-prototypes -Wformat=2
CFLAGS ?= -O2 -g
override CFLAGS += -std=c11 -fPIC $(WARNINGS)
LDLIBS := -lz

OBJECT := $(BUILD_DIR)/kilix_assets.o
LIB := $(BUILD_DIR)/lib$(PROJECT).a
TEST := $(BUILD_DIR)/test-assets
CHECK := $(BUILD_DIR)/kilix-assets-check

.PHONY: all clean install sanitize test

all: $(LIB)

$(BUILD_DIR):
	mkdir -p $@

$(OBJECT): src/kilix_assets.c include/kilix_assets.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

$(LIB): $(OBJECT)
	$(AR) rcs $@ $^

$(TEST): tests/test_assets.c $(LIB) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(LIB) $(LDLIBS) -o $@

$(CHECK): tools/kilix_assets_check.c $(LIB) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(LIB) $(LDLIBS) -o $@

test: $(TEST) $(CHECK)
	$(TEST)

sanitize: | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) -std=c11 -O1 -g3 $(WARNINGS) \
		-fno-omit-frame-pointer -fsanitize=address,undefined \
		src/kilix_assets.c tests/test_assets.c $(LDLIBS) \
		-fsanitize=address,undefined -o $(BUILD_DIR)/test-assets-sanitize
	ASAN_OPTIONS=detect_leaks=1 $(BUILD_DIR)/test-assets-sanitize

install: $(LIB)
	$(INSTALL) -d $(DESTDIR)$(PREFIX)/include $(DESTDIR)$(PREFIX)/lib
	$(INSTALL) -m 0644 include/kilix_assets.h $(DESTDIR)$(PREFIX)/include/
	$(INSTALL) -m 0644 $(LIB) $(DESTDIR)$(PREFIX)/lib/

clean:
	rm -rf $(BUILD_DIR)

-include $(OBJECT:.o=.d)
