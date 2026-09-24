# Changelog

## 0.6.0 - unreleased

- Add `kilix_game_policy`: a bounded, allocation-free-after-load runtime for
  tiny dense ReLU policy networks. Versioned `KXPOLICY` blobs carry a
  calibration temperature and an FNV-1a-64 digest. Damaged, truncated,
  oversized or non-finite blobs are refused. Also adds argmax and softmax
  helpers. Layers may be up to 1024 units wide, which covers board-game
  policies that read a whole table (e.g. 690 inputs, 681 actions); a blob
  wider than a reader's bound is refused as a shape error, never truncated.
- Add `tools/kilix_policy.py` to pack, verify and embed policy blobs, with a
  drift check for generated headers. C and Python tests share one reference
  blob digest.

## 0.5.0 - 2026-08-09

- Saturate extreme caller-supplied clock deltas before fixed-step clamping.
- Preserve pre-existing crash-signal dispositions and make the sanitizer
  build compatible with the runtime signal contract.
- Validate aggregate audio-table bounds and replace repeated cue-slot scans
  with bounded direct tracking.
- Reject overflowing image spans and write PPM rows in bounded chunks.
- Make dependency roots and build flags composable, isolate diagnostic builds,
  and add Clang, analyzer, install, benchmark, and release-gate coverage.
- Advance the minor version for the reviewed aggregate dependency layouts;
  all static consumers must rebuild with one consistent header/object set.
