# kilix-game-kit

This component is maintained in [`kilix-game-sdk`](..). Games pin the SDK,
not this directory as a separate repository.

`kilix-game-kit` is the thin integration layer for C games and apps running
under Kilix, Pleb, and Plebian OS. One recursively checked-out submodule pins
and builds the shared terminal, input, software-rendering, audio, and state
stack:

- `kitty-terminal-session` (including framebuffer, input, and keyboard)
- `soft-raster`
- `pcm-mixer` and `pcmmix-bank`
- `kilix-state`

The archive adds a fixed-step clock with bounded catch-up, an optional
terminal host with reversible signal handling, and a semantic audio runtime
over `pcm-mixer`. A separate test archive provides PTY I/O, CLI dispatch,
stable render suites, canonical PPM output, byte matching, hashes, and
tolerance-aware RGBA diffs. Game-specific rules, controls, art, simulation,
cue meaning, and music-scene meaning stay in each game.

The recursively pinned state library also exposes bounded little-endian
payload readers/writers and migration dispatch through
`kilix_state_codec.h`. Games retain their schemas and migration logic while
sharing the overflow, truncation, canonical-boolean, version-selection, and
zero-padding checks.

## Checkout and verify

```sh
git clone --recurse-submodules \
  https://github.com/itsmygithubacct/kilix-game-sdk.git
cd kilix-game-sdk/kilix-game-kit
make test
make sanitize
make test-clang
make analyze
make test-install
make benchmark
make test-deps
```

`tools/check-submodules.sh` fails for missing, conflicted, or locally advanced
dependency checkouts. The Git links are the dependency lock; updates are
ordinary reviewed submodule commits.

## Consume from a game

Add the SDK as `third_party/kilix-game-sdk`, include its path fragment, then
include game-kit’s Make fragment:

```make
KILIX_GAME_SDK_DIR ?= third_party/kilix-game-sdk
include $(KILIX_GAME_SDK_DIR)/mk/kilix-game-sdk.mk
include $(KILIX_GAME_KIT_DIR)/mk/game-kit.mk
CPPFLAGS += $(KILIX_GAME_KIT_CPPFLAGS)

game: $(GAME_OBJECTS) $(KILIX_GAME_KIT_LIB)
	$(CC) -o $@ $(GAME_OBJECTS) $(KILIX_GAME_KIT_LIB) \
		$(KILIX_GAME_KIT_LDLIBS)
```

Consumers can include `kilix_game_kit.h` for the complete runtime stack or
the individual public headers. Since the static archive stores dependencies
as separate objects, the linker pulls only the APIs a game uses. The fragment
preserves the consumer Makefile's default-goal selection instead of making its
internal archive rule the implicit target, exports the POSIX feature-test
macros required by the public signal and terminal structures, and also exports
the existing `KITTY_FRAMEBUFFER_DIR`, `KITTY_KEYBOARD_DIR`,
`SOFT_RASTER_DIR`, and `PCM_MIXER_DIR` names so migrations can retain useful
header and asset-validation prerequisites while deleting duplicate vendor
object recipes. Installed-header consumers that do not use the fragment must
define `_POSIX_C_SOURCE=200809L` and `_DEFAULT_SOURCE` before including system
headers, as exercised by `make test-install`.

## Fixed-step clock

```c
kilix_game_clock clock;
kilix_game_clock_init(&clock, NULL); /* 60 Hz, max 8 catch-up steps */

for (;;) {
    kilix_game_frame frame = kilix_game_clock_advance(
        &clock, kilix_game_monotonic_ns());
    for (uint32_t i = 0; i < frame.steps; ++i)
        update(kilix_game_clock_step_seconds(&clock));
    render(frame.alpha);
}
```

Long stalls are clamped and excess accumulated steps are dropped, preventing
a permanent “spiral of death.” The clock reports dropped time for diagnostics
and accepts caller-supplied timestamps, so simulation timing is deterministic
in tests. Even the full signed timestamp range is handled without arithmetic
overflow before the configured frame clamp is applied.

## Runtime host

`kilix_game_host_run()` composes the clock with `kitty-terminal-session`. It
starts callbacks only after the terminal is active and always unwinds callback
state, terminal modes, and the previous signal handlers in reverse order.
SIGINT, SIGTERM, SIGHUP, and SIGQUIT request an orderly stop; SIGPIPE is
temporarily ignored. SIGTSTP first restores the terminal and stops the process;
after SIGCONT the host restarts the terminal session over retained framebuffer
high-water storage and resets its fixed-step clock so suspended wall time
cannot advance simulation. Final shutdown releases the retained buffers.
Headless mode and a frame limit make the same host usable for smoke tests.
The signal scope restores whatever dispositions were active before it was
installed; they need not have been defaults. The sanitizer target disables
ASan's competing SIGSEGV interception while it verifies this deliberate
runtime ownership.

Games provide start, ordered-input-event, fixed-step, render, and stop
callbacks. They retain full ownership of simulation and framebuffer content.
`kilix_game_event_letter()` replaces the repeated case-insensitive shortcut
helper in game entry points.

## Semantic audio

`kilix_game_audio` resolves required or optional WAV cue tables from an
environment override, source root, or installed data root, then owns the
`pcmmix_bank` and mixer lifecycle. Callers play semantic cue IDs on master,
SFX, UI, ambience, or music buses. Bus gain can change immediately or fade
over game time. Music responds while it is active; one-shot and held-voice
gain is captured when each voice is scheduled, matching the underlying
handle-based mixer API.

Music-scene tables map game-owned scene IDs to bank samples. Selecting a new
scene uses `pcm-mixer`'s two-slot crossfade; selecting the current scene only
retargets volume. A missing optional sink leaves a ready, silent runtime,
while offline mode runs the exact mixer path in tests.

## Policy networks

`kilix_game_policy` runs tiny trained agents (CPU opponents, bots, NPC
steering) inside a game's fixed-step loop. A policy is a dense multilayer
perceptron with ReLU hidden layers and a linear output, stored as a versioned
little-endian blob: `KXPOLICY` magic, layer widths, a calibration temperature,
float32 parameters and an FNV-1a-64 digest. `kilix_policy_load()` copies the
blob into one owned allocation. Truncation, trailing bytes, foreign magic,
unknown versions, shapes beyond 8 layers or 256 units, non-finite values and
digest mismatches are all rejected, and a failed load leaves nothing
allocated. `kilix_policy_forward()` then allocates nothing and evaluates in a
fixed order, so a replay stays bit-identical within one build.
`kilix_policy_argmax()` and `kilix_policy_softmax()` turn logits into an
action or calibrated probabilities.

`tools/kilix_policy.py` (standard library only) packs raw float32 parameters
exported from a trainer into a blob, verifies one, and embeds it as a
generated C header. `embed --check` fails when a checked-in header has drifted
from its blob, which lets a game compile its weights in and still prove where
they came from. The format carries no training code or data: each game keeps
its own trainer and provenance record next to the blob it ships.

## Test helpers

Link `libkilix-game-test.a` with `-lutil` to create fixed-size PTYs and assert
terminal mode negotiation without a real terminal. The command-table helper
standardizes `--selftest`-style dispatch. `kilix_test_golden_suite` combines
per-state hashes into a deterministic suite hash, and
`kilix_test_write_ppm_rgba()` writes reviewable render artifacts.
`kilix_test_diff_rgba()` reports pixel differences at a selectable per-channel
tolerance. PPM conversion is chunked, so artifact time scales with pixel work
rather than one stdio call per pixel.

## Compatibility

Game-kit 0.6 adds the `kilix_policy_*` functions and `kilix_game_policy.h`;
the existing `kilix_game_*` and `kilix_test_*` functions and layouts are
unchanged from 0.5.

Game-kit 0.5 keeps the same `kilix_game_*` and `kilix_test_*` function set.
The reviewed terminal dependency enlarges the public aggregate host object, so
all static consumers must rebuild with a consistent set of 0.5 headers,
dependency headers, and archive objects; mixing the former host layout with
the reviewed terminal archive is unsupported.

## License

MIT. Pinned dependencies retain their own licenses.
