PROJECT := kilix-render-3d
BUILD_DIR ?= build
PREFIX ?= /usr/local
DESTDIR ?=
CC ?= cc
AR ?= ar
INSTALL ?= install
CPPFLAGS += -Iinclude
WARNINGS := -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Wstrict-prototypes -Wmissing-prototypes -Wformat=2
CFLAGS ?= -O2 -g
CFLAGS += -std=c11 -fPIC $(WARNINGS) -MMD -MP
LDLIBS += -lm
SOURCES := src/math.c src/renderer.c
OBJECTS := $(SOURCES:src/%.c=$(BUILD_DIR)/%.o)
LIB := $(BUILD_DIR)/lib$(PROJECT).a
TEST := $(BUILD_DIR)/test-render-3d
PC := $(BUILD_DIR)/$(PROJECT).pc
.PHONY: all test sanitize clean install
all: $(LIB)
$(BUILD_DIR):
	mkdir -p $@
$(BUILD_DIR)/%.o: src/%.c include/kr3d.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(LIB): $(OBJECTS)
	$(AR) rcs $@ $^
$(TEST): tests/test_render3d.c $(LIB)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(LIB) $(LDLIBS) -o $@
test: $(TEST)
	$(TEST)
sanitize:
	mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) -std=c11 -O1 -g3 $(WARNINGS) -fno-omit-frame-pointer \
		-fsanitize=address,undefined $(SOURCES) tests/test_render3d.c \
		-fsanitize=address,undefined $(LDLIBS) -o $(BUILD_DIR)/test-render-3d-sanitize
	ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 $(BUILD_DIR)/test-render-3d-sanitize
$(PC): kilix-render-3d.pc.in Makefile | $(BUILD_DIR)
	sed 's|@PREFIX@|$(PREFIX)|g' $< > $@
install: $(LIB) $(PC)
	$(INSTALL) -d $(DESTDIR)$(PREFIX)/include $(DESTDIR)$(PREFIX)/lib/pkgconfig
	$(INSTALL) -m 0644 include/kr3d.h $(DESTDIR)$(PREFIX)/include/
	$(INSTALL) -m 0644 $(LIB) $(DESTDIR)$(PREFIX)/lib/
	$(INSTALL) -m 0644 $(PC) $(DESTDIR)$(PREFIX)/lib/pkgconfig/
clean:
	rm -rf $(BUILD_DIR)
-include $(OBJECTS:.o=.d)
