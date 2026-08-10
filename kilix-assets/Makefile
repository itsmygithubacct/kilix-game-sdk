PROJECT := kilix-assets
BUILD_DIR ?= build
PREFIX ?= /usr/local
DESTDIR ?=

CC ?= cc
AR ?= ar
INSTALL ?= install

override CPPFLAGS += -D_POSIX_C_SOURCE=200809L -Iinclude
WARNINGS := -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
	-Wstrict-prototypes -Wmissing-prototypes -Wformat=2
CFLAGS ?= -O2 -g
override CFLAGS += -std=c11 -fPIC $(WARNINGS) $(EXTRA_CFLAGS)
override LDFLAGS += $(EXTRA_LDFLAGS)
override LDLIBS += -lz
DEPFLAGS := -MMD -MP

OBJECT := $(BUILD_DIR)/kilix_assets.o
LIB := $(BUILD_DIR)/lib$(PROJECT).a
TEST := $(BUILD_DIR)/test-assets
CHECK := $(BUILD_DIR)/kilix-assets-check

.PHONY: all clean install sanitize test

all: $(LIB)

$(BUILD_DIR):
	mkdir -p $@

$(OBJECT): src/kilix_assets.c include/kilix_assets.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

$(LIB): $(OBJECT)
	$(AR) rcs $@ $^

$(TEST): tests/test_assets.c $(LIB) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) $< $(LIB) $(LDFLAGS) \
		$(LDLIBS) -o $@

$(CHECK): tools/kilix_assets_check.c $(LIB) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) $< $(LIB) $(LDFLAGS) \
		$(LDLIBS) -o $@

test: $(TEST) $(CHECK)
	$(TEST)
	@set -e; fixture=$$(mktemp -d); \
		trap 'rm -rf "$$fixture"' EXIT; \
		$(TEST) --write-checker-fixture "$$fixture"; \
		$(CHECK) "$$fixture/manifest.json" "$$fixture"

SANITIZE_BUILD_DIR ?= $(BUILD_DIR)/sanitize
sanitize:
	@ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
		$(MAKE) --no-print-directory BUILD_DIR="$(SANITIZE_BUILD_DIR)" \
		EXTRA_CFLAGS="-O1 -fno-omit-frame-pointer \
		-fsanitize=address,undefined" \
		EXTRA_LDFLAGS="-fsanitize=address,undefined" test

install: $(LIB)
	$(INSTALL) -d $(DESTDIR)$(PREFIX)/include $(DESTDIR)$(PREFIX)/lib
	$(INSTALL) -m 0644 include/kilix_assets.h $(DESTDIR)$(PREFIX)/include/
	$(INSTALL) -m 0644 $(LIB) $(DESTDIR)$(PREFIX)/lib/

clean:
	rm -rf $(BUILD_DIR)

-include $(OBJECT:.o=.d) $(TEST).d $(CHECK).d
