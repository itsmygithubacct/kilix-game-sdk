# Include this file from a game Makefile after adding kilix-game-kit as a
# recursive submodule. Consumers link one archive and keep their own targets.

KILIX_GAME_KIT_DEFAULT_GOAL_BEFORE_INCLUDE := $(.DEFAULT_GOAL)

ifndef KILIX_GAME_KIT_ROOT
KILIX_GAME_KIT_ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/..)
endif
KILIX_GAME_KIT_BUILD_DIR ?= $(KILIX_GAME_KIT_ROOT)/build
KILIX_GAME_KIT_LIB := $(KILIX_GAME_KIT_BUILD_DIR)/libkilix-game-kit.a

# Compatibility path names let existing game Makefiles collapse their vendor
# object rules incrementally while keeping asset-validation dependencies.
KITTY_TERMINAL_SESSION_DIR ?= $(KILIX_GAME_KIT_ROOT)/third_party/kitty-terminal-session
KITTY_FRAMEBUFFER_DIR ?= $(KITTY_TERMINAL_SESSION_DIR)/third_party/kitty-framebuffer
KITTY_INPUT_DIR ?= $(KITTY_TERMINAL_SESSION_DIR)/third_party/kitty-input
KITTY_KEYBOARD_DIR ?= $(KITTY_INPUT_DIR)/third_party/kitty_keyboard
SOFT_RASTER_DIR ?= $(KILIX_GAME_KIT_ROOT)/third_party/soft-raster
PCM_MIXER_DIR ?= $(KILIX_GAME_KIT_ROOT)/third_party/pcm-mixer
KILIX_STATE_DIR ?= $(KILIX_GAME_KIT_ROOT)/third_party/kilix-state

KILIX_GAME_KIT_CPPFLAGS := \
	-D_POSIX_C_SOURCE=200809L \
	-D_DEFAULT_SOURCE \
	-I$(KILIX_GAME_KIT_ROOT)/include \
	-I$(KITTY_TERMINAL_SESSION_DIR)/include \
	-I$(KITTY_FRAMEBUFFER_DIR)/include \
	-I$(KITTY_INPUT_DIR)/include \
	-I$(KITTY_KEYBOARD_DIR)/include \
	-I$(SOFT_RASTER_DIR)/include \
	-I$(PCM_MIXER_DIR)/include \
	-I$(KILIX_STATE_DIR)/include
KILIX_GAME_KIT_LDLIBS := -lz -lpthread -lm

# Keep the consumer-facing archive target real (so unchanged applications do
# not relink on every make invocation), but include every source/header that
# can affect the embedded archive.  The recursive make then performs the exact
# object-level dependency check.
KILIX_GAME_KIT_BUILD_INPUTS := \
	$(KILIX_GAME_KIT_ROOT)/Makefile \
	$(wildcard $(KILIX_GAME_KIT_ROOT)/include/*.h) \
	$(wildcard $(KILIX_GAME_KIT_ROOT)/src/*.c) \
	$(wildcard $(KITTY_TERMINAL_SESSION_DIR)/include/*.h) \
	$(wildcard $(KITTY_TERMINAL_SESSION_DIR)/src/*.c) \
	$(wildcard $(KITTY_FRAMEBUFFER_DIR)/include/*.h) \
	$(wildcard $(KITTY_FRAMEBUFFER_DIR)/src/*.c) \
	$(wildcard $(KITTY_INPUT_DIR)/include/*.h) \
	$(wildcard $(KITTY_INPUT_DIR)/src/*.c) \
	$(wildcard $(KITTY_KEYBOARD_DIR)/include/*.h) \
	$(wildcard $(KITTY_KEYBOARD_DIR)/src/*.c) \
	$(wildcard $(SOFT_RASTER_DIR)/include/*.h) \
	$(wildcard $(SOFT_RASTER_DIR)/src/*.c) \
	$(wildcard $(SOFT_RASTER_DIR)/src/*.h) \
	$(wildcard $(PCM_MIXER_DIR)/include/*.h) \
	$(wildcard $(PCM_MIXER_DIR)/src/*.c) \
	$(wildcard $(KILIX_STATE_DIR)/include/*.h) \
	$(wildcard $(KILIX_STATE_DIR)/src/*.c)

$(KILIX_GAME_KIT_LIB): $(KILIX_GAME_KIT_BUILD_INPUTS)
	$(MAKE) -C $(KILIX_GAME_KIT_ROOT) \
		BUILD_DIR=$(KILIX_GAME_KIT_BUILD_DIR) $(KILIX_GAME_KIT_LIB)

# An included fragment must not become the consumer's implicit default goal.
# If no target existed before this include, clear the archive rule selected by
# GNU Make so the next target in the consumer Makefile becomes the default.
ifeq ($(KILIX_GAME_KIT_DEFAULT_GOAL_BEFORE_INCLUDE),)
.DEFAULT_GOAL :=
endif
