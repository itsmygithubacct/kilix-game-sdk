# kilix-game-sdk

Every shared Kilix game library in one repository and one recursively pinned
revision. The SDK keeps the component APIs and archives separate while making
cross-library changes atomic.

It is not installed on its own. Games and desktops pin it as the submodule
`third_party/kilix-game-sdk` and compile the components they use into their
binaries. The games install on demand from the Kilix content catalog
(`kilix games play <id>` or `kilix install <id>`).

## What it does

- Gives a C game one dependency to pin: the terminal session, input, software
  rasterizer, mixer and state store, plus the engine layers above them.
- Supplies a fixed-step clock, a terminal host with reversible signal handling,
  semantic audio buses, tiny policy-network inference, and test helpers (PTYs,
  golden render hashes, PPM output, RGBA diffs) through `kilix-game-kit`.
- Adds optional layers: asset loading and atlases, story state and dialogue,
  spatial grids and paths, top-down and isometric renderers, a scalar 3D
  renderer, game-rule-free UI widgets, and Python validation tooling.
- Publishes every component path from one Make fragment, so a game includes only
  the fragments it uses.
- Keeps each component's commit history beneath its directory.

| Component | Purpose |
|---|---|
| `kilix-game-kit` | Terminal, input, software-rendering, audio, state, fixed-step runtime, and test support |
| `kilix-assets` | Bounded PNG/raw-RGBA loading, manifests, atlases, caching, and animation |
| `kilix-story` | Conditions, transactional actions, and dialogue traversal |
| `kilix-world` | Projection-independent grids, paths, sight, regions, interactions, and portals |
| `kilix-render-3d` | Checked 3D math, indexed resources, and deterministic scalar rendering |
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

| Make target | What it does |
|---|---|
| `make` | builds every C component against the shared `soft-raster` and game kit |
| `make test` | checks each Make fragment keeps the consumer's default goal, then runs every component's tests, including `kilix-game-tools` |
| `make sanitize` | the C components' sanitizer targets |
| `make check-submodules` | `tools/check-submodules.sh`: fails on a missing, conflicted or locally advanced pinned checkout |

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

## How it works

```
 game Makefile
   └─ mk/kilix-game-sdk.mk        sets KILIX_GAME_KIT_DIR, KILIX_ASSETS_DIR, KILIX_UI_DIR, ...
        ├─ kilix-game-kit/mk/game-kit.mk ─▶ libkilix-game-kit.a
        │     third_party/kitty-terminal-session ─▶ kitty-framebuffer, kitty-input
        │     third_party/soft-raster
        │     third_party/pcm-mixer
        │     third_party/kilix-state
        └─ kilix-assets, kilix-story, kilix-world, kilix-top-down-engine,
           kilix-tactics-engine, kilix-render-3d, kilix-ui (each its own fragment and archive)
```

The four external libraries are Git submodules under
`kilix-game-kit/third_party/`; the Git links are the dependency lock, and a
game advances all of them at once by moving its single SDK gitlink. Each game
builds its own copy of the archives with its own flags. Because the game-kit
archive stores dependencies as separate objects, the linker pulls in only the
APIs a game calls.

## Compatibility

Existing games may keep their established paths such as
`third_party/kilix-game-kit`: replace each former component submodule with a
relative symlink into `third_party/kilix-game-sdk/`. The repository then has
one Git pin without requiring an unrelated build-system rewrite.

Every component retains its original commit history beneath its directory.

## Documentation

Each component documents itself:

- [kilix-game-kit/README.md](kilix-game-kit/README.md): the runtime stack,
  fixed-step clock, runtime host, semantic audio, policy networks, test helpers
  and compatibility notes.
- [kilix-assets/README.md](kilix-assets/README.md): asset loading, manifests,
  atlases and animation.
- [kilix-story/README.md](kilix-story/README.md): story state, actions and
  dialogue.
- [kilix-world/README.md](kilix-world/README.md): grids, paths, sight, regions
  and portals.
- [kilix-render-3d/README.md](kilix-render-3d/README.md): the scalar 3D
  renderer.
- [kilix-top-down-engine/README.md](kilix-top-down-engine/README.md) and
  [its integration guide](kilix-top-down-engine/docs/integration.md): the
  orthographic renderer.
- [kilix-tactics-engine/README.md](kilix-tactics-engine/README.md) and
  [its integration guide](kilix-tactics-engine/docs/integration.md): the
  isometric tactics library.
- [kilix-ui/README.md](kilix-ui/README.md): menus, panels, dialogue and meters.
- [kilix-game-tools/README.md](kilix-game-tools/README.md): Python validation
  and deterministic release archives.

Most components also keep a `CHANGELOG.md` in their directory.

## Related repositories

Pinned dependencies, under `kilix-game-kit/third_party/`:
[kitty-terminal-session](https://github.com/itsmygithubacct/kitty-terminal-session#readme)
(which pins [kitty-framebuffer](https://github.com/itsmygithubacct/kitty-framebuffer#readme)
and [kitty-input](https://github.com/itsmygithubacct/kitty-input#readme)),
[soft-raster](https://github.com/itsmygithubacct/soft-raster#readme),
[pcm-mixer](https://github.com/itsmygithubacct/pcm-mixer#readme) and
[kilix-state](https://github.com/itsmygithubacct/kilix-state#readme).

Projects that pin the SDK at `third_party/kilix-game-sdk`:

- [kilix-games](https://github.com/itsmygithubacct/kilix-games#readme), whose
  C games share one SDK checkout.
- [pleb-tower](https://github.com/itsmygithubacct/pleb-tower#readme),
  [super-kilix](https://github.com/itsmygithubacct/super-kilix#readme),
  [pleb-plant-grower](https://github.com/itsmygithubacct/pleb-plant-grower#readme)
  and [kilix-land](https://github.com/itsmygithubacct/kilix-land#readme).
- The desktops [kilix-cap](https://github.com/itsmygithubacct/kilix-cap#readme)
  and [kilix-land-desktop](https://github.com/itsmygithubacct/kilix-land-desktop#readme),
  also carried as components of
  [kilix-desktop-sdk](https://github.com/itsmygithubacct/kilix-desktop-sdk#readme),
  and [kilix-land-agent](https://github.com/itsmygithubacct/kilix-land-agent#readme).

## Part of Plebian-OS

kilix-game-sdk is one of the repositories behind
[Plebian-OS](https://github.com/itsmygithubacct/plebian-os#readme), a Debian
install whose desktop is [Kilix](https://github.com/itsmygithubacct/kilix#readme)
running the [Kilix 95](https://github.com/itsmygithubacct/kilix-95#readme)
desktop through [Pleb](https://github.com/itsmygithubacct/pleb#readme).
It reaches a user compiled into the Kilix games, which install on demand from
the Kilix content catalog, and into the Kilix Cap and Kilix Land desktops. The
map of every repository and how it ships is on the
[components page](https://github.com/itsmygithubacct/plebian-os/blob/main/docs/components.md).

## License

MIT; see [LICENSE](LICENSE). Components that carry their own `LICENSE` file keep
it, and the pinned dependencies retain their own licences.
