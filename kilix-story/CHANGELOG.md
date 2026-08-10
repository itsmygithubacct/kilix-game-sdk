# Changelog

## 0.2.0

- Reject overlapping, misaligned, or arithmetically invalid state storage,
  closing an aliased flag/counter path to signed overflow.
- Give empty `all` and `any` lists their conventional identities and leave
  condition outputs unchanged when any entry is invalid.
- Validate condition operations, ranges, action operations, and graph links
  before a session starts; check every graph state index against the bound
  state.
- Make failed session starts preserve the existing session and propagate
  evaluation errors distinctly from unavailable choices.
- Index common action projections and graphs with bounded stack tables while
  retaining allocation-free exact fallbacks for larger inputs.
- Harden pointer/count arithmetic, dependency tracking, caller build-flag
  composition, sanitizer isolation, and Clang test isolation.
- Expand the tests with malformed inputs, transaction checks, both graph
  validation paths, and 20,000 deterministic action-model comparisons.

The 15-function public C surface, result and operation values, and public
structure layouts are unchanged from 0.1.0. The corrected semantics and
stricter malformed-input rejection motivate the minor-version change.

## 0.1.0

- Initial caller-owned story state, conditions, transactional actions, graph
  validation, and dialogue sessions.
