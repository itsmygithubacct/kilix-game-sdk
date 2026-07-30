# kilix-game-kit

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
  https://github.com/itsmygithubacct/kilix-game-kit.git
cd kilix-game-kit
make test
make sanitize
make test-deps
```

`tools/check-submodules.sh` fails for missing, conflicted, or locally advanced
dependency checkouts. The Git links are the dependency lock; updates are
ordinary reviewed submodule commits.

## Consume from a game

Add this repository as `third_party/kilix-game-kit`, initialize recursively,
then include its Make fragment:

```make
include third_party/kilix-game-kit/mk/game-kit.mk
CPPFLAGS += $(KILIX_GAME_KIT_CPPFLAGS)

game: $(GAME_OBJECTS) $(KILIX_GAME_KIT_LIB)
	$(CC) -o $@ $(GAME_OBJECTS) $(KILIX_GAME_KIT_LIB) \
		$(KILIX_GAME_KIT_LDLIBS)
```

Consumers can include `kilix_game_kit.h` for the complete runtime stack or
the individual public headers. Since the static archive stores dependencies
as separate objects, the linker pulls only the APIs a game uses. The fragment
also exports the existing `KITTY_FRAMEBUFFER_DIR`, `KITTY_KEYBOARD_DIR`,
`SOFT_RASTER_DIR`, and `PCM_MIXER_DIR` names so migrations can retain useful
header and asset-validation prerequisites while deleting duplicate vendor
object recipes.

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
in tests.

## Runtime host

`kilix_game_host_run()` composes the clock with `kitty-terminal-session`. It
starts callbacks only after the terminal is active and always unwinds callback
state, terminal modes, and the previous signal handlers in reverse order.
SIGINT, SIGTERM, SIGHUP, and SIGQUIT request an orderly stop; SIGPIPE is
temporarily ignored. Headless mode and a frame limit make the same host usable
for smoke tests.

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

## Test helpers

Link `libkilix-game-test.a` with `-lutil` to create fixed-size PTYs and assert
terminal mode negotiation without a real terminal. The command-table helper
standardizes `--selftest`-style dispatch. `kilix_test_golden_suite` combines
per-state hashes into a deterministic suite hash, and
`kilix_test_write_ppm_rgba()` writes reviewable render artifacts.
`kilix_test_diff_rgba()` reports pixel differences at a selectable per-channel
tolerance.

## License

MIT. Pinned dependencies retain their own licenses.
