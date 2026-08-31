# Changelog

## 0.1.0

- Add a read-only classifier for the packaged Steam system layer.
- Emit a bounded install-requirements document with distinct Valve-terms and
  Valve-archive/i386 authorization gates.
- Fail closed until the F100 mediator supplies an opaque, validated
  `kilix.install.authorization/v2` result.
- Bind the packaged launcher to `/usr/bin/steam` and its exact root-owned
  `/usr/lib/steam/bin_steam.sh` target.
- Keep session launch fail closed until Kilix supplies the named Steam-private
  display and descendant-scope profile.
- Freeze the 9/9 install states, 6/6 terminal outcomes, 7/7 client-update
  states, and 9/9 non-failure tab lifecycle states in the public contract.
- Add structured, bounded diagnostics and pollable lifecycle handles.
- Treat an unavailable or ambiguous process-table scan as a non-launching
  permission result rather than evidence that no unrelated Steam exists.
- Report a path the trust rules refused separately from a system layer that
  disagrees. A helper, launcher or policy reached through a directory other
  users may write to was previously classified `conflicting` with
  `system-layer-conflicting`, which claims the file was read and did not match;
  in fact it was never read. Such a run now returns
  `KVALVE_CLIENT_ERR_PERMISSION` with classification `unknown` and
  `system-layer-untrusted-path`, and the summary names the offending directory
  with its mode and owner. A policy that genuinely disagrees is unchanged.
