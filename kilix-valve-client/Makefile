PROJECT := kilix-valve-client
BUILD_DIR ?= build
PREFIX ?= /usr/local
DESTDIR ?=

CC ?= cc
AR ?= ar
INSTALL ?= install

override CPPFLAGS += -D_GNU_SOURCE -D_POSIX_C_SOURCE=200809L -Iinclude
WARNINGS := -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
	-Wstrict-prototypes -Wmissing-prototypes -Wformat=2
CFLAGS ?= -O2 -g
override CFLAGS += -std=c11 -fPIC $(WARNINGS) $(EXTRA_CFLAGS)
override LDFLAGS += $(EXTRA_LDFLAGS)
DEPFLAGS := -MMD -MP

SOURCES := src/context.c src/detect.c src/install.c src/session.c \
	src/process.c src/diagnostics.c
OBJECTS := $(patsubst src/%.c,$(BUILD_DIR)/%.o,$(SOURCES))
TEST_OBJECTS := $(patsubst src/%.c,$(BUILD_DIR)/test/%.o,$(SOURCES))
LIB := $(BUILD_DIR)/lib$(PROJECT).a
CLI := $(BUILD_DIR)/$(PROJECT)
TEST := $(BUILD_DIR)/test-valve-client
HEADER_TEST := $(BUILD_DIR)/test-header
FAKE_STEAM := $(BUILD_DIR)/fake-steam

.PHONY: all clean install sanitize test

all: $(LIB) $(CLI)

$(BUILD_DIR) $(BUILD_DIR)/test:
	mkdir -p $@

$(BUILD_DIR)/%.o: src/%.c include/kilix_valve_client.h src/internal.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

$(BUILD_DIR)/test/%.o: src/%.c include/kilix_valve_client.h src/internal.h | $(BUILD_DIR)/test
	$(CC) $(CPPFLAGS) -DKVALVE_CLIENT_TESTING $(CFLAGS) $(DEPFLAGS) -c $< -o $@

$(LIB): $(OBJECTS)
	$(AR) rcs $@ $^

$(CLI): tools/kilix-valve-client.c $(LIB) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) $< $(LIB) $(LDFLAGS) -o $@

$(FAKE_STEAM): tests/fake_steam.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) $< $(LDFLAGS) -o $@

$(TEST): tests/test_valve_client.c $(TEST_OBJECTS) $(FAKE_STEAM) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) -DKVALVE_CLIENT_TESTING $(CFLAGS) $(DEPFLAGS) \
		-DKVALVE_TEST_FAKE_STEAM='"$(abspath $(FAKE_STEAM))"' \
		tests/test_valve_client.c $(TEST_OBJECTS) $(LDFLAGS) -o $@

$(HEADER_TEST): tests/test_header.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) $< $(LDFLAGS) -o $@

test: $(TEST) $(HEADER_TEST) $(CLI)
	$(TEST)
	$(HEADER_TEST)
	$(CLI) --help >/dev/null
	@set +e; $(CLI) plan-install >$(BUILD_DIR)/plan.json; code=$$?; \
		test $$code -eq 3; \
		grep -q 'kilix.install.license/v1' $(BUILD_DIR)/plan.json; \
		grep -q 'kilix.install.authorization/v2' $(BUILD_DIR)/plan.json; \
		grep -q '"combined_confirmation_allowed": false' $(BUILD_DIR)/plan.json

SANITIZE_BUILD_DIR ?= $(BUILD_DIR)/sanitize
sanitize:
	@ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
	UBSAN_OPTIONS=halt_on_error=1 \
	$(MAKE) --no-print-directory BUILD_DIR="$(SANITIZE_BUILD_DIR)" \
		EXTRA_CFLAGS="-O1 -fno-omit-frame-pointer -fsanitize=address,undefined" \
		EXTRA_LDFLAGS="-fsanitize=address,undefined" test

install: $(LIB) $(CLI)
	$(INSTALL) -d $(DESTDIR)$(PREFIX)/include $(DESTDIR)$(PREFIX)/lib \
		$(DESTDIR)$(PREFIX)/bin
	$(INSTALL) -m 0644 include/kilix_valve_client.h \
		$(DESTDIR)$(PREFIX)/include/
	$(INSTALL) -m 0644 $(LIB) $(DESTDIR)$(PREFIX)/lib/
	$(INSTALL) -m 0755 $(CLI) $(DESTDIR)$(PREFIX)/bin/

clean:
	rm -rf $(BUILD_DIR)

-include $(OBJECTS:.o=.d) $(TEST_OBJECTS:.o=.d) $(CLI).d $(TEST).d \
	$(HEADER_TEST).d $(FAKE_STEAM).d
