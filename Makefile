# kilix-tactics-engine — shared tactical spatial engine.
#
# Produces libkilix-tactics-core.a (standard library only) and
# libkilix-tactics-soft.a (draws into a caller-owned sr_canvas).

CC ?= cc
AR ?= ar
BUILD_DIR ?= build
SOFT_RASTER_DIR ?= third_party/soft-raster

WARNINGS := -Wall -Wextra -Werror -Wshadow -Wconversion -Wsign-conversion \
	-Wstrict-prototypes -Wmissing-prototypes -Wpointer-arith -Wcast-qual \
	-Wvla
CFLAGS ?= -O2 -g
CFLAGS += -std=c11 -pedantic $(WARNINGS)
CPPFLAGS += -Iinclude

CORE_LIB := $(BUILD_DIR)/libkilix-tactics-core.a
SOFT_LIB := $(BUILD_DIR)/libkilix-tactics-soft.a

CORE_SOURCES := src/types.c src/map.c src/projection.c src/nav.c src/sight.c \
	src/render_queue.c
CORE_OBJECTS := $(CORE_SOURCES:%.c=$(BUILD_DIR)/%.o)

SOFT_SOURCES := src/render_soft.c
SOFT_OBJECTS := $(SOFT_SOURCES:%.c=$(BUILD_DIR)/%.o)

PUBLIC_HEADERS := $(wildcard include/*.h)

TEST_SOURCES := tests/test_tactics.c
TEST_BIN := $(BUILD_DIR)/test-tactics
PROP_SOURCES := tests/test_properties.c
PROP_BIN := $(BUILD_DIR)/test-properties

.PHONY: all core soft test sanitize test-clang test-headers clean

all: core soft

core: $(CORE_LIB)

soft: $(SOFT_LIB)

$(BUILD_DIR)/%.o: %.c $(PUBLIC_HEADERS)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(SOFT_CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/src/render_soft.o: SOFT_CPPFLAGS := -I$(SOFT_RASTER_DIR)/include

$(CORE_LIB): $(CORE_OBJECTS)
	@mkdir -p $(dir $@)
	$(AR) rcs $@ $^

$(SOFT_LIB): $(SOFT_OBJECTS)
	@mkdir -p $(dir $@)
	$(AR) rcs $@ $^

$(TEST_BIN): $(TEST_SOURCES) $(CORE_LIB)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(TEST_SOURCES) $(CORE_LIB) -o $@ -lm

$(PROP_BIN): $(PROP_SOURCES) $(CORE_LIB)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(PROP_SOURCES) $(CORE_LIB) -o $@ -lm

test: $(TEST_BIN) $(PROP_BIN)
	$(TEST_BIN)
	$(PROP_BIN)

sanitize:
	$(MAKE) clean
	$(MAKE) BUILD_DIR=$(BUILD_DIR)-san \
		CFLAGS="-O1 -g -fsanitize=address,undefined \
			-fno-omit-frame-pointer -fno-sanitize-recover=all" test

test-clang:
	$(MAKE) clean
	$(MAKE) CC=clang BUILD_DIR=$(BUILD_DIR)-clang test

# Every public header must compile standalone and be self-sufficient.
test-headers: | $(BUILD_DIR)
	@for header in $(notdir $(PUBLIC_HEADERS)); do \
		echo "#include <$$header>" > $(BUILD_DIR)/header-check.c; \
		echo "int main(void) { return 0; }" >> $(BUILD_DIR)/header-check.c; \
		$(CC) $(CPPFLAGS) -I$(SOFT_RASTER_DIR)/include $(CFLAGS) \
			$(BUILD_DIR)/header-check.c -o $(BUILD_DIR)/header-check \
			|| exit 1; \
		echo "  ok $$header"; \
	done
	@rm -f $(BUILD_DIR)/header-check.c $(BUILD_DIR)/header-check

$(BUILD_DIR):
	@mkdir -p $@

clean:
	rm -rf $(BUILD_DIR) $(BUILD_DIR)-san $(BUILD_DIR)-clang
