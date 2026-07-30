# Include this file from a game Makefile after adding kilix-game-kit as a
# recursive submodule. Consumers link one archive and keep their own targets.

KILIX_GAME_KIT_ROOT ?= $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/..)
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
	-I$(KILIX_GAME_KIT_ROOT)/include \
	-I$(KILIX_GAME_KIT_ROOT)/third_party/kitty-terminal-session/include \
	-I$(KILIX_GAME_KIT_ROOT)/third_party/kitty-terminal-session/third_party/kitty-framebuffer/include \
	-I$(KILIX_GAME_KIT_ROOT)/third_party/kitty-terminal-session/third_party/kitty-input/include \
	-I$(KILIX_GAME_KIT_ROOT)/third_party/kitty-terminal-session/third_party/kitty-input/third_party/kitty_keyboard/include \
	-I$(KILIX_GAME_KIT_ROOT)/third_party/soft-raster/include \
	-I$(KILIX_GAME_KIT_ROOT)/third_party/pcm-mixer/include \
	-I$(KILIX_GAME_KIT_ROOT)/third_party/kilix-state/include
KILIX_GAME_KIT_LDLIBS := -lz -lpthread -lm

# Keep the consumer-facing archive target real (so unchanged applications do
# not relink on every make invocation), but include every source/header that
# can affect the embedded archive.  The recursive make then performs the exact
# object-level dependency check.
KILIX_GAME_KIT_BUILD_INPUTS := \
	$(KILIX_GAME_KIT_ROOT)/Makefile \
	$(wildcard $(KILIX_GAME_KIT_ROOT)/include/*.h) \
	$(wildcard $(KILIX_GAME_KIT_ROOT)/src/*.c) \
	$(wildcard $(KILIX_GAME_KIT_ROOT)/third_party/kitty-terminal-session/include/*.h) \
	$(wildcard $(KILIX_GAME_KIT_ROOT)/third_party/kitty-terminal-session/src/*.c) \
	$(wildcard $(KILIX_GAME_KIT_ROOT)/third_party/kitty-terminal-session/third_party/kitty-framebuffer/include/*.h) \
	$(wildcard $(KILIX_GAME_KIT_ROOT)/third_party/kitty-terminal-session/third_party/kitty-framebuffer/src/*.c) \
	$(wildcard $(KILIX_GAME_KIT_ROOT)/third_party/kitty-terminal-session/third_party/kitty-input/include/*.h) \
	$(wildcard $(KILIX_GAME_KIT_ROOT)/third_party/kitty-terminal-session/third_party/kitty-input/src/*.c) \
	$(wildcard $(KILIX_GAME_KIT_ROOT)/third_party/kitty-terminal-session/third_party/kitty-input/third_party/kitty_keyboard/include/*.h) \
	$(wildcard $(KILIX_GAME_KIT_ROOT)/third_party/kitty-terminal-session/third_party/kitty-input/third_party/kitty_keyboard/src/*.c) \
	$(wildcard $(KILIX_GAME_KIT_ROOT)/third_party/soft-raster/include/*.h) \
	$(wildcard $(KILIX_GAME_KIT_ROOT)/third_party/soft-raster/src/*.c) \
	$(wildcard $(KILIX_GAME_KIT_ROOT)/third_party/soft-raster/src/*.h) \
	$(wildcard $(KILIX_GAME_KIT_ROOT)/third_party/pcm-mixer/include/*.h) \
	$(wildcard $(KILIX_GAME_KIT_ROOT)/third_party/pcm-mixer/src/*.c) \
	$(wildcard $(KILIX_GAME_KIT_ROOT)/third_party/kilix-state/include/*.h) \
	$(wildcard $(KILIX_GAME_KIT_ROOT)/third_party/kilix-state/src/*.c)

$(KILIX_GAME_KIT_LIB): $(KILIX_GAME_KIT_BUILD_INPUTS)
	$(MAKE) -C $(KILIX_GAME_KIT_ROOT) \
		BUILD_DIR=$(KILIX_GAME_KIT_BUILD_DIR) $(KILIX_GAME_KIT_LIB)
