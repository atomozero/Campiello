# Discovery filesystem design note (Campiello network neighborhood in Tracker)

Status: **proposed, not started.** Presents the live Bricola peer set as a folder in Tracker,
the BeOS "network neighborhood" experience, done natively. Read together with `docs/M1.md`
(the FUSE front end and `PeerBackend` it reuses) and `docs/VERIFIED.md` sections 1 and 11.

## Goal
A "Campiello" volume whose root folder lists the machines discovered on the LAN, live: a peer
appears as a folder when it comes online and vanishes when it leaves, with no refresh. Opening
a peer's folder shows that machine's shared "Condivisa" contents, browsable in Tracker like any
directory.

## The BeOS mechanism we emulate
BeOS `netfs` presented network shares inside a folder, and the folder updated by itself because
the filesystem pushed **node-monitor notifications** to the kernel (entry created / removed),
which Tracker watches. Two ingredients: (1) a filesystem whose directory listing is a live data
source, not on-disk entries; (2) that filesystem telling the VFS when entries appear/disappear
so open Tracker windows refresh. Campiello reproduces both: Bricola is the live data source,
and the discovery filesystem emits the notifications.

## Architecture: a router filesystem
One userlandfs filesystem with two layers, reusing pieces already built:

```
Tracker
  |
userlandfs  (the discovery volume, mounted top-level as /Campiello)
  |
Router front end                          <- NEW
  |   root ("/")            -> the peer list, from a browse-only Bricola   (Bricola: DONE)
  |   subtree ("/<peer>/…") -> that peer's share, via a per-peer CnpBackend (CnpBackend: DONE)
  |
Bricola (discovery)        CnpBackend -> TLS -> the peer's Campiello daemon -> its Condivisa
```

- **Root directory = Bricola.** The add-on owns one browse-only `Bricola`. `readdir("/")`
  returns one entry per currently-known peer (the friendly instance label, deduplicated /
  sanitized for the filesystem). `getattr("/<peer>")` reports a directory.
- **Each peer subtree = a CnpBackend.** The first access under `/<peer>/…` opens (lazily) a
  TLS-pinned CNP connection to that peer's advertised host:port and drives a `CnpBackend`;
  `readdir`/`getattr`/`open`/`read` under that subtree map 1:1 to `CnpBackend`
  `ReadDir`/`Stat`/`Open`/`Read` on the peer. So the peer's typed-attribute share is browsable.
  Connections are pooled and idle-closed.

The path router is the only genuinely new logic: split a request path into `<peer>` +
`<remainder>`, look up (or create) that peer's backend, and delegate. It sits above the same
`PeerBackend` abstraction M1 already routes through, so the FUSE callback layer
(`CampielloFuse` / `FuseTranslate`) is reused; only the "which backend for this path" step is
new.

## Live updates: the notification problem (and why it steers the front end)
Tracker refreshes an open folder when the filesystem posts `notify_entry_created` /
`notify_entry_removed` for that directory. The **native `fs_interface`** exposes these calls;
the **FUSE 2.x high-level API does not** (it is pull-only: Tracker re-reads `readdir` on manual
refresh or reopen). So there is a fork:

- **v1 (FUSE, no live push):** the root folder shows peers, but only refreshes when Tracker
  re-reads it (open/refresh). Simple, and it avoids the native-front-end unmount hazard
  (`VERIFIED.md` section 1: the FUSE bridge unmounts cleanly; the native `fs_interface` module
  KDL'd on unmount). Good for a first, safe, demonstrable version.
- **v2 (native `fs_interface`, live push):** the Bricola observer calls
  `notify_entry_created`/`_removed` so open Tracker windows update by themselves - the real
  BeOS feel. This needs the native front end, whose **unmount KDL hazard must be fixed in a
  throwaway VM first** (already an open item). It also unlocks typed BFS attributes end to end.

Recommendation: build v1 on FUSE to prove the router + Tracker experience without the hazard,
then move to v2 for live notifications once the unmount issue is resolved.

## Trust (this is where client-side trust gets exercised)
Entering a peer's folder connects to it as a **client**, which is the path the current code
exercises least. Reusing Traghetto: mutual-auth TLS with SPKI pinning, and trust-on-first-use.

- The peer being opened raises its one-tap "Allow?" prompt on ITS side (the existing
  `GatedHandler` + `Pairing`); until allowed, its share is empty / access-denied, surfaced as a
  friendly "in attesa di autorizzazione" state, not an error.
- This side pins the peer's key on first successful connect (client-side TOFU), and a later key
  change re-raises the decision rather than trusting silently. The client-side trust store and
  the "pin the server we connect to" flow are thinner today than the server side and are part
  of this work.
- Discovery stays untrusted input: the `fp` TXT hint and the friendly name only label a
  candidate; the identity is the pinned key (`docs/PROPOSAL.md` section 9).

## Where it appears
Mount the discovery volume at a top-level path (e.g. `/Campiello`), so Tracker shows it as a
disk on the Desktop (learned in M1: nested mounts get no Desktop icon). The volume is a single,
always-available entry; the peers live inside it. A later refinement can auto-mount it at login
via the resident daemon.

## What a peer folder contains
The peer's shared root (its "Condivisa"), served by its Campiello daemon over CNP, with typed
BFS attributes on the native path. Read-only first (browse + read); write follows the M3
per-peer read-only default. Interop (SFTP) peers are a separate surface (M1) and do not appear
here - this folder is native Campiello discovery.

## Reuse vs new
- Reuse: `Bricola` (discovery, done, runtime-verified), `CnpBackend` (client read+write, done),
  the FUSE front end `CampielloFuse` + `FuseTranslate` (done), the TLS/trust stack (done).
- New: the **path router** (peer <-> subtree dispatch), a **per-peer connection manager**
  (lazy connect, pool, idle-close, the client-side pairing/pinning), the **discovery add-on
  main** (owns Bricola + the router, mounts at /Campiello), and for v2 the **native
  `fs_interface` module** + Bricola-to-`notify_entry_*` bridge.

## Phased commit plan
1. **Path router over PeerBackend (pure std, testable).** `PathRouter` (`src/fondamenta/
   discovery/`): root lists peers from an injected `PeerSource`, `/<peer>/<rest>` delegates to
   that peer's backend, handles namespaced. Unit-tested off Haiku with fakes. **[done]**
2. **Connection manager:** `ConnectionManager` maps a peer (from an injected `PeerDirectory`) to
   a lazily-connected `CnpBackend` over pinned TLS, with client-side TOFU. Loopback-tested
   against a real `ServerNode` via `PathRouter` (`test_connmanager`). **[done]**
3. **Discovery add-on (FUSE, v1):** `campiello_net_main.cpp` owns a browse-only Bricola, a
   `BricolaDirectory` (Bricola peers -> endpoints), the `ConnectionManager`, and a `PathRouter`,
   served by `CampielloFuseMain`. The whole add-on links (`tests/discovery/` net-addon).
   **[done; manual mount at /Campiello is the runtime test.]**
4. **Live notifications (v2, native):** port the front end to an `fs_interface` module and push
   `notify_entry_created`/`_removed` from the Bricola observer. Gated on the unmount-hazard fix
   in a VM. **[not started - needs the throwaway VM.]**
5. **Packaging + auto-mount:** ship in the hpkg; the resident daemon mounts `/Campiello` at
   startup so the neighborhood is just there.

## To verify before/while coding
- `notify_entry_created` / `notify_entry_removed` signatures against
  `headers/os/drivers/fs_interface.h` (for v2).
- How the FUSE bridge reports a directory whose contents change between reads (does Tracker
  re-list on window focus / F5?), to set v1 expectations.
- Same-host multi-process mDNS delivery (the demo showed a browser window not receiving a
  separate process's announcements; on two machines it is one socket per host, but confirm the
  add-on's Bricola receives real-LAN announcements).
- Client-side pairing UX: what the user sees while a peer folder waits for the remote allow.
