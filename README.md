# kilix-game-sdk

Every shared Kilix game library in one repository and one recursively pinned
revision. The SDK keeps the component APIs and archives separate while making
cross-library changes atomic.

| Component | Purpose |
|---|---|
| `kilix-game-kit` | Terminal, input, software-rendering, audio, state, fixed-step runtime, and test support |
| `kilix-assets` | Bounded PNG/raw-RGBA loading, manifests, atlases, caching, and animation |
| `kilix-story` | Conditions, transactional actions, and dialogue traversal |
| `kilix-world` | Projection-independent grids, paths, sight, regions, interactions, and portals |
| `kilix-top-down-engine` | Orthographic cameras, framebuffers, and pixel-art drawing |
| `kilix-tactics-engine` | Isometric projection, picking, paths, sight, cover, and draw ordering |
| `kilix-ui` | Game-rule-free menus, panels, dialogue, meters, and RPG composites |
| `kilix-game-tools` | Deterministic Python validation and release archives |

Game rules, content schemas, art, audio, campaign compilers, and release policy
remain in each game.

## Checkout and verify

```sh
git clone --recurse-submodules \
  https://github.com/itsmygithubacct/kilix-game-sdk.git
cd kilix-game-sdk
make test
```

The SDK owns one copy of each external runtime dependency below
`kilix-game-kit/third_party/`. The renderers and interface component reuse that
same `soft-raster` checkout.

## Consume from a game

Add one recursive submodule:

```sh
git submodule add \
  https://github.com/itsmygithubacct/kilix-game-sdk.git \
  third_party/kilix-game-sdk
git submodule update --init --recursive
```

The shared Make fragment publishes every component path:

```make
KILIX_GAME_SDK_DIR ?= third_party/kilix-game-sdk
include $(KILIX_GAME_SDK_DIR)/mk/kilix-game-sdk.mk

include $(KILIX_GAME_KIT_DIR)/mk/game-kit.mk
include $(KILIX_TOP_DOWN_DIR)/mk/kilix-top-down.mk
include $(KILIX_ASSETS_DIR)/mk/kilix-assets.mk
include $(KILIX_UI_DIR)/mk/kilix-ui.mk
```

Include only the component fragments a game uses. `kilix-content` deliberately
remains a separate cross-stack dependency because Kilix desktops and the core
terminal consume it too.

## Compatibility

Existing games may keep their established paths such as
`third_party/kilix-game-kit`: replace each former component submodule with a
relative symlink into `third_party/kilix-game-sdk/`. The repository then has
one Git pin without requiring an unrelated build-system rewrite.

Every component retains its original commit history beneath its directory.
