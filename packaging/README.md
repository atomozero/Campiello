# Packaging

Builds the Campiello Haiku package (`.hpkg`). Haiku-only: needs the `package` tool, the
userlandfs FUSE headers (`userland_fs` package), and `libssh2_devel`.

## Build

    make package

produces `campiello-0.1.0-3-x86_64.hpkg`. `make clean` removes the build tree, the add-on,
and the package.

## What it contains

Two surfaces:

- **Interop (SFTP):** the `campiello_sftp` userlandfs add-on (`add-ons/userlandfs/`) and the
  `campiello_mount` connect helper (`apps/`). See "Mounting an SFTP host" below.
- **Native (Haiku-to-Haiku):** the resident `campiello_daemon` (`bin/`), which advertises
  `_campiello._tcp` and serves your shared "Condivisa" folder, and the `campiello_net` discovery
  add-on (`add-ons/userlandfs/`), which shows the machines found on the LAN as a `/Campiello`
  network-neighborhood volume. See "Native mode" below.

`requires` declares the runtime dependencies: `userland_fs` (the FUSE bridge), `lib:libssh2`
(SFTP), and `lib:libssl` / `lib:libcrypto` (native TLS).

## Native mode

The resident daemon **starts automatically at login** (via the user launch job
`data/user_launch/campiello`): it serves `<home>/Desktop/Condivisa` and advertises on the LAN.
To start it now without logging out: `launch_roster start x-vnd.Campiello-daemon` (or just run
`campiello_daemon &`).

On another Haiku box with the package installed, the daemon is likewise running. Mount the
discovery volume to see the machines as folders:

    mkdir -p /Campiello
    mount -t userlandfs -p "campiello_net" /Campiello

Browse `/Campiello` in Tracker: each discovered machine is a folder; opening one connects (the
remote raises a one-tap allow prompt the first time) and shows its shared files.

Discovery needs at least **two machines** running the daemon on the same LAN: same-host
cross-process multicast is not delivered on Haiku (see docs/VERIFIED.md), so a single machine
cannot discover itself.

## Install

    pkgman install ./campiello-0.1.0-3-x86_64.hpkg

or copy it into `~/config/packages` (per-user) or `/system/packages` (system-wide). Haiku
activates the package and the add-on becomes available to `mount -t userlandfs`.

## Mounting

Run the connect helper (`campiello_mount`, in your apps): enter the host, user, and password
(or a private-key path), and click Connetti. It validates the login, pins the host key on
first use with a one-tap prompt, mounts the host read-only, and opens the folder in Tracker.
Discovery and pairing (which remove even the host entry) arrive in later milestones.

The equivalent manual mount, for scripting:

    mkdir -p /tmp/win
    mount -t userlandfs -o ro -p "campiello_sftp host=<ip> user=<user> password=<pass>" /tmp/win

Then browse `/tmp/win` in Tracker. `unmount /tmp/win` to detach. See `docs/M1.md` for the open
items (notably the error-return sign convention to confirm at the first mount).
