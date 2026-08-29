# kilix-valve-client

`kilix-valve-client` is the unprivileged Steam orchestration component used by
Kilix. It detects the Plebian OS system layer, describes the optional install,
and exposes a fail-closed session API for the fixed `/usr/bin/steam` package
entry point. It neither contains nor redistributes Valve software, and the
launcher remains disabled until Kilix supplies the exact private profile.

The component deliberately separates two decisions:

1. Valve terms are handled by `kilix.install.license/v1`.
2. Adding Valve's standing APT source and enabling system-wide i386 require a
   different confirmation and a `kilix.install.authorization/v2` record.

A terms click never authorizes the source or architecture change. Until the
F100 authority returns an opaque pre-mutation validation path, the install API
returns `KVALVE_CLIENT_ERR_AUTHORIZATION_REQUIRED` and executes no helper.
The library never accepts a password, token, repository URL, signing key,
package name, helper path, launcher path, shell fragment, or public display
override from its public API.

## Display and process boundary

The public session entry point currently fails closed with
`steam-session-profile-unavailable`. The admitted Kilix shared-output v1 host
does not yet attest the tab-private Xwayland/Xauthority pair and owned user
scope needed to retain Steam, helper, game, pressure-vessel, and Proton
descendants across daemonize and re-exec. The ordinary shared socket is not
treated as equivalent, and no launcher runs while the named profile is 0/1.

There is no dormant shared-display launcher path to enable: the future provider
must return an opaque, authenticated `steam-v1` capability covering the private
Wayland and Xwayland identities, owned descendant scope, input admission,
resize acknowledgement, and bounded teardown. F102 will integrate that returned
contract rather than guessing its endpoint or scope format.

The private display contains windows. It is not a filesystem, network, D-Bus,
GPU, audio, device, or Valve-account sandbox. Steam and games remain native
code running as the desktop user, with the user's session D-Bus and audio
services available. Kilix owns presentation, input escape, resize, and the
outer lifecycle.

An exact unrelated launcher process is reported and is never attached to,
reparented, or signalled. No name-wide stop implementation is present. The
future provider must terminate only its opaque owned scope. Steam's mutable
home data, libraries, prefixes, saves, workshop content, credentials, and
updater payloads are never parsed or removed.

## Build and inspect

```sh
make
make test
make sanitize
build/kilix-valve-client status
build/kilix-valve-client plan-install
```

`status`, `doctor`, and `plan-install` are read-only. The requirements document
contains no authorization record and is not permission to mutate the system.

Steam and Valve are trademarks of Valve Corporation. This project is not
affiliated with or endorsed by Valve Corporation.
