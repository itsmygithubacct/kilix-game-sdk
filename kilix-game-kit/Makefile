PROJECT := kilix-game-kit
BUILD_DIR ?= build
PREFIX ?= /usr/local
DESTDIR ?=

CC ?= cc
CXX ?= c++
AR ?= ar
INSTALL ?= install

KITTY_TERMINAL_SESSION_DIR ?= third_party/kitty-terminal-session
KITTY_FRAMEBUFFER_DIR ?= $(KITTY_TERMINAL_SESSION_DIR)/third_party/kitty-framebuffer
KITTY_INPUT_DIR ?= $(KITTY_TERMINAL_SESSION_DIR)/third_party/kitty-input
KITTY_KEYBOARD_DIR ?= $(KITTY_INPUT_DIR)/third_party/kitty_keyboard
SOFT_RASTER_DIR ?= third_party/soft-raster
PCM_MIXER_DIR ?= third_party/pcm-mixer
KILIX_STATE_DIR ?= third_party/kilix-state

# Retain the original private names for direct standalone overrides.
KITTYTS_DIR ?= $(KITTY_TERMINAL_SESSION_DIR)
KITTYFB_DIR ?= $(KITTY_FRAMEBUFFER_DIR)
KITTYIN_DIR ?= $(KITTY_INPUT_DIR)
KITTYKB_DIR ?= $(KITTY_KEYBOARD_DIR)
RASTER_DIR ?= $(SOFT_RASTER_DIR)
PCMMIX_DIR ?= $(PCM_MIXER_DIR)
STATE_DIR ?= $(KILIX_STATE_DIR)

override CPPFLAGS += -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE \
	-Iinclude -I$(KITTYTS_DIR)/include -I$(KITTYFB_DIR)/include \
	-I$(KITTYIN_DIR)/include -I$(KITTYKB_DIR)/include \
	-I$(RASTER_DIR)/include -I$(PCMMIX_DIR)/include -I$(STATE_DIR)/include \
	$(EXTRA_CPPFLAGS)
WARNINGS := \
	-Wall -Wextra -Wpedantic -Wconversion -Wshadow \
	-Wstrict-prototypes -Wmissing-prototypes -Wformat=2
CFLAGS ?= -O2 -g
override CFLAGS += -std=c11 -fPIC -pthread $(WARNINGS) $(EXTRA_CFLAGS)
override LDFLAGS += $(EXTRA_LDFLAGS)
override LDLIBS += -lz -lpthread -lm $(EXTRA_LDLIBS)
DEPFLAGS := -MMD -MP

OBJECTS := \
	$(BUILD_DIR)/kilix_game_loop.o \
	$(BUILD_DIR)/kilix_game_runtime.o \
	$(BUILD_DIR)/kilix_game_audio.o \
	$(BUILD_DIR)/kitty_terminal_session.o \
	$(BUILD_DIR)/kitty_framebuffer.o \
	$(BUILD_DIR)/kitty_input.o \
	$(BUILD_DIR)/kitty_input_posix.o \
	$(BUILD_DIR)/kitty_keyboard.o \
	$(BUILD_DIR)/kitty_keyboard_posix.o \
	$(BUILD_DIR)/soft_raster.o \
	$(BUILD_DIR)/pcm_mixer.o \
	$(BUILD_DIR)/pcm_wav.o \
	$(BUILD_DIR)/pcmmix_bank.o \
	$(BUILD_DIR)/kilix_state.o \
	$(BUILD_DIR)/kilix_state_codec.o
LIB := $(BUILD_DIR)/lib$(PROJECT).a
TEST_OBJECT := $(BUILD_DIR)/kilix_game_test.o
TEST_LIB := $(BUILD_DIR)/libkilix-game-test.a
TEST_BIN := $(BUILD_DIR)/test-game-kit
BENCHMARK_BIN := $(BUILD_DIR)/benchmark-game-kit
DEPS := $(OBJECTS:.o=.d) $(TEST_OBJECT:.o=.d) $(TEST_BIN).d \
	$(BENCHMARK_BIN).d

.PHONY: all analyze benchmark clean install release-gate sanitize test \
	test-clang test-deps test-install test-make-fragment

all: $(LIB) $(TEST_LIB)

$(BUILD_DIR):
	mkdir -p $@

$(BUILD_DIR)/kilix_game_loop.o: src/kilix_game_loop.c include/kilix_game_loop.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

$(BUILD_DIR)/kilix_game_runtime.o: src/kilix_game_runtime.c include/kilix_game_runtime.h include/kilix_game_loop.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

$(BUILD_DIR)/kilix_game_audio.o: src/kilix_game_audio.c include/kilix_game_audio.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

$(BUILD_DIR)/kilix_game_test.o: src/kilix_game_test.c include/kilix_game_test.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

$(BUILD_DIR)/kitty_terminal_session.o: $(KITTYTS_DIR)/src/kitty_terminal_session.c $(KITTYTS_DIR)/include/kitty_terminal_session.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

$(BUILD_DIR)/kitty_framebuffer.o: $(KITTYFB_DIR)/src/kitty_framebuffer.c $(KITTYFB_DIR)/include/kitty_framebuffer.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

$(BUILD_DIR)/kitty_input.o: $(KITTYIN_DIR)/src/kitty_input.c $(KITTYIN_DIR)/include/kitty_input.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

$(BUILD_DIR)/kitty_input_posix.o: $(KITTYIN_DIR)/src/kitty_input_posix.c $(KITTYIN_DIR)/include/kitty_input_posix.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

$(BUILD_DIR)/kitty_keyboard.o: $(KITTYKB_DIR)/src/kitty_keyboard.c $(KITTYKB_DIR)/include/kitty_keyboard.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

$(BUILD_DIR)/kitty_keyboard_posix.o: $(KITTYKB_DIR)/src/kitty_keyboard_posix.c $(KITTYKB_DIR)/include/kitty_keyboard_posix.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

$(BUILD_DIR)/soft_raster.o: $(RASTER_DIR)/src/soft_raster.c $(RASTER_DIR)/include/soft_raster.h $(RASTER_DIR)/src/font8x16.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

$(BUILD_DIR)/pcm_mixer.o: $(PCMMIX_DIR)/src/pcm_mixer.c $(PCMMIX_DIR)/include/pcm_mixer.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

$(BUILD_DIR)/pcm_wav.o: $(PCMMIX_DIR)/src/pcm_wav.c $(PCMMIX_DIR)/include/pcm_mixer.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

$(BUILD_DIR)/pcmmix_bank.o: $(PCMMIX_DIR)/src/pcmmix_bank.c $(PCMMIX_DIR)/include/pcmmix_bank.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

$(BUILD_DIR)/kilix_state.o: $(STATE_DIR)/src/kilix_state.c $(STATE_DIR)/include/kilix_state.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

$(BUILD_DIR)/kilix_state_codec.o: $(STATE_DIR)/src/kilix_state_codec.c \
		$(STATE_DIR)/include/kilix_state_codec.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

$(LIB): $(OBJECTS)
	$(AR) rcs $@ $^

$(TEST_LIB): $(TEST_OBJECT)
	$(AR) rcs $@ $^

$(TEST_BIN): tests/test_game_kit.c $(LIB) $(TEST_LIB) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) $< $(TEST_LIB) $(LIB) \
		$(LDFLAGS) $(LDLIBS) -lutil -o $@

$(BENCHMARK_BIN): benchmarks/benchmark_game_kit.c $(LIB) $(TEST_LIB) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) $< $(TEST_LIB) $(LIB) \
		$(LDFLAGS) $(LDLIBS) -lutil -o $@

test: $(TEST_BIN) test-make-fragment
	$(TEST_BIN)
	tools/check-submodules.sh

test-make-fragment:
	$(MAKE) -f tests/test_make_fragment.mk check-root

benchmark: $(BENCHMARK_BIN)
	$(BENCHMARK_BIN)

test-deps:
	$(MAKE) -C $(KITTYTS_DIR) test
	$(MAKE) -C $(RASTER_DIR) test
	$(MAKE) -C $(PCMMIX_DIR) test
	$(MAKE) -C $(STATE_DIR) test

SANITIZE_BUILD_DIR ?= $(BUILD_DIR)/sanitize
sanitize:
	@ASAN_OPTIONS=detect_leaks=1:halt_on_error=1:handle_segv=0 \
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

ANALYZE_BUILD_DIR ?= $(BUILD_DIR)/analyze
analyze:
	$(MAKE) --no-print-directory BUILD_DIR="$(ANALYZE_BUILD_DIR)" clean
	$(MAKE) --no-print-directory BUILD_DIR="$(ANALYZE_BUILD_DIR)" \
		EXTRA_CFLAGS="-O0 -fanalyzer -Werror" all

test-install:
	sh tests/test_install.sh "$(CC)" "$(CXX)"

release-gate: test test-clang sanitize analyze test-install benchmark

install: all
	$(INSTALL) -d $(DESTDIR)$(PREFIX)/include $(DESTDIR)$(PREFIX)/lib
	$(INSTALL) -m 0644 include/kilix_game_kit.h include/kilix_game_loop.h \
		include/kilix_game_runtime.h include/kilix_game_audio.h \
		include/kilix_game_test.h \
		$(KITTYTS_DIR)/include/kitty_terminal_session.h \
		$(KITTYFB_DIR)/include/kitty_framebuffer.h \
		$(KITTYIN_DIR)/include/kitty_input.h \
		$(KITTYIN_DIR)/include/kitty_input_posix.h \
		$(KITTYKB_DIR)/include/kitty_keyboard.h \
		$(KITTYKB_DIR)/include/kitty_keyboard_posix.h \
		$(RASTER_DIR)/include/soft_raster.h \
		$(PCMMIX_DIR)/include/pcm_mixer.h \
		$(PCMMIX_DIR)/include/pcmmix_bank.h \
		$(STATE_DIR)/include/kilix_state.h \
		$(STATE_DIR)/include/kilix_state_codec.h \
		$(DESTDIR)$(PREFIX)/include/
	$(INSTALL) -m 0644 $(LIB) $(TEST_LIB) $(DESTDIR)$(PREFIX)/lib/

clean:
	rm -rf $(BUILD_DIR)

-include $(DEPS)
