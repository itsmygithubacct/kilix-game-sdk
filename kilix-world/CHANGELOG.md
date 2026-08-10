# Changelog

## 0.3.0 — 2026-08-09

- Reject overlapping, misaligned, arithmetically invalid, or oversized search
  workspaces transactionally, including output/workspace aliases.
- Represent a valid `UINT32_MAX` path cost and distinguish an unrepresentable
  reachable frontier from a disconnected goal.
- Preserve established direction-sensitive Bresenham tie behavior while
  making line-of-sight errors transactional and eliminating repeated grid
  validation in the ray loop.
- Make path, reachable, and bulk top-down failure outputs transactional; bulk
  conversion now rejects invalid empty grids and non-finite path centers.
- Stop reachable searches at the requested cost frontier, remove redundant
  bounds/conversion and callback work, and use direct internal cell/index
  operations after preflight.
- Add bounded hash validation for ordinary map/record/portal catalogs while
  retaining an exact allocation-free fallback for larger inputs.
- Preserve required build flags with caller-supplied variables, add test
  dependency tracking and extra-flag hooks, and isolate Clang and sanitizer
  builds.
- Expand tests with workspace/alias contracts, numeric limits, transactional
  outputs, hash/fallback boundaries, 5,000 visibility compatibility cases,
  and 10,000 deterministic navigation/reference-model cases.
