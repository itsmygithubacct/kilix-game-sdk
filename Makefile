GAME_KIT := $(CURDIR)/kilix-game-kit
SOFT_RASTER := $(GAME_KIT)/third_party/soft-raster
TOP_DOWN := $(CURDIR)/kilix-top-down-engine

.PHONY: all test sanitize clean check-submodules

all: check-submodules
	$(MAKE) -C kilix-game-kit all
	$(MAKE) -C kilix-assets all
	$(MAKE) -C kilix-story all
	$(MAKE) -C kilix-world all
	$(MAKE) -C kilix-tactics-engine \
		SOFT_RASTER_DIR="$(SOFT_RASTER)" all
	$(MAKE) -C kilix-top-down-engine \
		SOFT_RASTER_DIR="$(SOFT_RASTER)" \
		KILIX_GAME_KIT_ROOT="$(GAME_KIT)" all
	$(MAKE) -C kilix-ui \
		KILIX_TOP_DOWN_DIR="$(TOP_DOWN)" \
		SOFT_RASTER_DIR="$(SOFT_RASTER)" all

test: check-submodules
	$(MAKE) -f tests/test_fragment_default_goal.mk \
		FRAGMENT=kilix-game-kit/mk/game-kit.mk consumer-default
	$(MAKE) -f tests/test_fragment_default_goal.mk \
		FRAGMENT=kilix-assets/mk/kilix-assets.mk consumer-default
	$(MAKE) -f tests/test_fragment_default_goal.mk \
		FRAGMENT=kilix-story/mk/kilix-story.mk consumer-default
	$(MAKE) -f tests/test_fragment_default_goal.mk \
		FRAGMENT=kilix-world/mk/kilix-world.mk consumer-default
	$(MAKE) -f tests/test_fragment_default_goal.mk \
		FRAGMENT=kilix-tactics-engine/mk/kilix-tactics.mk consumer-default
	$(MAKE) -f tests/test_fragment_default_goal.mk \
		FRAGMENT=kilix-top-down-engine/mk/kilix-top-down.mk consumer-default
	$(MAKE) -f tests/test_fragment_default_goal.mk \
		FRAGMENT=kilix-ui/mk/kilix-ui.mk consumer-default
	$(MAKE) -C kilix-game-kit test
	$(MAKE) -C kilix-assets test
	$(MAKE) -C kilix-story test
	$(MAKE) -C kilix-world test
	$(MAKE) -C kilix-tactics-engine \
		SOFT_RASTER_DIR="$(SOFT_RASTER)" all test test-headers
	$(MAKE) -C kilix-top-down-engine \
		SOFT_RASTER_DIR="$(SOFT_RASTER)" \
		KILIX_GAME_KIT_ROOT="$(GAME_KIT)" \
		test test-integration test-headers
	$(MAKE) -C kilix-ui \
		KILIX_TOP_DOWN_DIR="$(TOP_DOWN)" \
		SOFT_RASTER_DIR="$(SOFT_RASTER)" test
	$(MAKE) -C kilix-game-tools test

sanitize: check-submodules
	$(MAKE) -C kilix-game-kit sanitize
	$(MAKE) -C kilix-assets sanitize
	$(MAKE) -C kilix-story sanitize
	$(MAKE) -C kilix-world sanitize
	$(MAKE) -C kilix-tactics-engine \
		SOFT_RASTER_DIR="$(SOFT_RASTER)" sanitize
	$(MAKE) -C kilix-top-down-engine \
		SOFT_RASTER_DIR="$(SOFT_RASTER)" sanitize
	$(MAKE) -C kilix-ui \
		KILIX_TOP_DOWN_DIR="$(TOP_DOWN)" \
		SOFT_RASTER_DIR="$(SOFT_RASTER)" sanitize

check-submodules:
	tools/check-submodules.sh

clean:
	$(MAKE) -C kilix-game-kit clean
	$(MAKE) -C kilix-assets clean
	$(MAKE) -C kilix-story clean
	$(MAKE) -C kilix-world clean
	$(MAKE) -C kilix-tactics-engine clean
	$(MAKE) -C kilix-top-down-engine clean
	$(MAKE) -C kilix-ui clean
