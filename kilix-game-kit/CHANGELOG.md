# Changelog

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
