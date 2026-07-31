# Campiello: a modern, native, MIT-licensed successor to World O' Networking for Haiku

Working title: **Campiello** (the small neighborhood square where Venetians gather, the literal translation of "network neighborhood"). Rename freely.

This document is the driving design context for the project. It states what is verified against the Haiku source, what is design intent, and what still needs verification, so that implementation work never rests on invented APIs.

---

## 1. Mission

Recreate the "wow" of WON (Windows and network shares appearing as browsable folders inside Tracker) on modern Haiku, without Samba, without GPL or LGPL code in the core, and with a native Haiku-to-Haiku mode that does something no SMB or NFS stack can do: preserve BFS extended attributes and MIME types end to end, and run live distributed queries across the LAN.

Two modes, one browser:

- **Interop mode**: mount remote shares from the rest of the world (SFTP, WebDAV, NFS4) so Haiku talks to Windows, Linux, macOS, and NAS boxes. Fully permissive licensing.
- **Native mode**: a Campiello-to-Campiello protocol with full BFS attribute fidelity and distributed live queries. MIT by construction because we write it.

Three non-negotiable principles, ranked above feature breadth. They are requirements, not polish, and a milestone is not done if it violates one:

1. **Install in a double click.** One hpkg, no terminal, no config files, no services to start by hand.
2. **First use at zero configuration.** Peers appear by themselves and you enter them with a double click. No IP, no mount command, no workgroup, no manual to read.
3. **Security that is strong and robust but invisible.** Real authentication and encryption under the hood, but the user never meets a key, a certificate, or a CA. The only thing they ever see is a one tap "allow this computer" prompt.

If a design choice makes the tool more capable but harder to install or understand, the easy path wins and the capability goes behind a default or an advanced toggle.

---

## 2. Scope and non-goals

Campiello is a LAN tool for a small, trusted set of personal machines. Naming the boundary keeps scope from drifting.

In scope:
- Local network peers, discovered automatically, on the same link or a reachable subnet.
- Haiku to Haiku native sharing with full attribute and query fidelity.
- Interop with Windows, Linux, macOS, and NAS over permissive protocols.

Non-goals, explicitly out for at least the first cycle:
- No WAN or internet federation, no relay servers, no cloud rendezvous. If two machines are not on the same network, Campiello does not connect them. Tunnel first, which is what Sotoportego is for, and they become local.
- No user accounts, no central directory, no login. Identity is per machine, established by pairing.
- No multi-user permission matrix. Sharing is per machine and per folder, not per-user ACLs.
- No mobile client in this cycle.
- Not a general purpose FUSE or mDNS toolkit. Every piece is scoped to Campiello's own need.
- Not a Samba replacement for the world. SMB stays a fallback add-on, see the Why not just use SMB section.

---

## 3. Verified ground truth (checked against haiku/haiku master)

Do not re-derive these from memory. They were confirmed by reading the source on the date this document was authored. Re-verify if the tree has moved.

### userlandfs is present and includes a FUSE compatibility layer
- Path: `src/add-ons/kernel/file_systems/userlandfs/` (contains `Jamfile`, `kernel_add_on/`, `private/`, `server/`, `shared/`).
- FUSE bridge lives at `src/add-ons/kernel/file_systems/userlandfs/server/fuse/`.
- FUSE headers at `headers/private/userlandfs/fuse/`: `fuse.h`, `fuse_common.h`, `fuse_lowlevel.h`, plus `*_compat.h` and `fuse_opt.h`.
- API level: **libfuse 2.9** (`fuse_common.h` defines `FUSE_MAJOR_VERSION 2`, `FUSE_MINOR_VERSION 9`). Both the high-level API (`struct fuse_operations` in `fuse.h`) and the low-level API (`fuse_lowlevel.h`) are present.
- Default `FUSE_USE_VERSION` is 21, with compat handling up to 26.
- **Implication**: a filesystem written against libfuse 2.x ports to Haiku through userlandfs. A filesystem written against libfuse 3.x does not port unchanged and must be adapted to the 2.x API.

### NFS clients are in-tree
- `src/add-ons/kernel/file_systems/nfs4/Jamfile` exists (NFS4 client).
- `src/add-ons/kernel/file_systems/nfs/Jamfile` exists (older NFS).
- **Implication**: NFS4 is a ready building block for interop mode, no new transport needed there.

### The Network Kit has no built-in service discovery
- `headers/os/net/` contains `NetworkAddressResolver.h` (plain DNS resolution) and the socket and address classes, but **no DNS-SD or mDNS class**.
- No `dns_sd.h` under `headers/posix/` or `headers/os/net/`.
- No mDNS responder library under `src/libs/` in core.
- **Implication**: discovery has to be added. See the Bricola discovery section.

### License facts that shape the architecture
- `libsmb2` (Sahlberg): the library and include directories are **LGPL-2.1** (confirmed in its `COPYING`). Not MIT-clean. Keep out of core.
- `libssh2`: **BSD-3-Clause** (confirmed in its `COPYING`). MIT-compatible. This is the SFTP transport for interop mode.
- Haiku itself is MIT.

---

## 4. Experience requirements: easy to install, easy to use, invisibly secure

This section is a first-class part of the spec. The architecture already makes most of "easy" almost free, because the same choices that recreate the WON effect are the ones that remove friction. The single hard part is reconciling "peers appear by themselves" with "but only the ones I allow", without exposing a single technical concept. That reconciliation is where the real UX and engineering effort goes.

### Easy to install
- **One hpkg installs everything**: Fondamenta (userlandfs add-on), Traghetto (daemon), and Bricola (discovery plus Deskbar replicant), activated in a single step.
- Distribution through HaikuDepot, a double click to install. `pkgman` remains for power users but is not the main path.
- Uninstall is clean because of packagefs. No leftover services or stray config.
- No post-install step. After install the daemon and replicant are running and discovery is live, with no user action.

### Easy to use
- **Lives inside Tracker.** Peers appear as mounted volumes where the user already expects network resources. Browse by double click, copy by drag and drop. No new app with new gestures to learn.
- **Zero configuration on first run.** mDNS/DNS-SD means peers show up by themselves. No IP entry, no mount command, no workgroup. This is the WON effect and the usability win at the same time, they are the same feature.
- **Deskbar replicant** shows who is online, in the spirit of Dogana and Sotoportego. It is glanceable, not a control panel.
- **No manual required.** A user who has never seen the tool should be able to open a peer and copy a file without instructions.

### Invisibly secure (strong under the hood, transparent on the surface)
WON was easy partly because it had no security, which is not acceptable today. The goal is to add real security without adding any visible complexity. The model is the LocalSend pairing flow, which is already familiar from the LocalSend native client.

- **First contact pairing.** The first time a peer tries to connect, the target machine shows a simple, friendly prompt: "Allow NomePC to connect?" with one tap to accept. Keys and trust are established invisibly underneath. The user only ever sees the accept.
- **Keys are never shown.** Identity keys are generated on install and stored locally. The user never sees, copies, types, or manages a key, a certificate, or a CA. If a concept like "certificate" would appear in the UI, that is a design bug.
- **Encryption is always on, never a choice.** Native mode is TLS 1.3 always. There is no "enable security" checkbox, because off is never an option.
- **Safe by default scope.** By default a node shares a single "Condivisa" folder, not the whole disk. Exposing more is a deliberate opt in, so nobody over-shares by accident.
- **Forgetting is easy too.** Revoking a previously allowed peer is one click in the replicant menu, no key management.

### Acceptance test for "easy" (used in milestones)
A person who has never seen Campiello, given only an installed system and no instructions, can: install from HaikuDepot in a double click, see another machine appear by itself, accept the one tap pairing prompt, open the peer in Tracker, and copy a file out of it. If any step needs a spoken or written instruction, the milestone is not done.

---

## 5. License policy (hard rule for the core)

Core code and anything statically linked into it must be permissive.

**Allowed in core**: MIT, BSD-2-Clause, BSD-3-Clause, Apache-2.0, ISC, zlib, public domain.

Concrete picks:
- SFTP transport: `libssh2` (BSD-3).
- TLS for native mode: mbedTLS (Apache-2.0) or the TLS already used by Haiku's `SecureSocket` (verify which backend Haiku links).
- All Haiku APIs: MIT.

**Not allowed in core**: GPL (Samba, smbclient), LGPL (libsmb2, libdsm, Avahi).

LGPL is permitted only as an **optional, separately packaged, dynamically loaded add-on**, never statically linked, never in the default build, with its own clearly partitioned source directory and its own license file. SMB interop, if ever wanted, goes here.

---

## 6. Why not just use SMB?

SMB is the obvious question, so here is the honest answer. SMB is not banned out of dogma, it is the wrong tool for the core and for native mode, and the right tool for exactly one narrow job. This document is written in English to match the rest of the doc, even though the question is often asked in Italian.

The fair case for SMB first, so this is not a strawman:
- It is the lingua franca of LAN file sharing. Every Windows machine, almost every NAS, plus macOS and Linux, speak it. Maximum reach with one protocol.
- Users already understand "network shares", so the mental model is familiar.
- For pure interop against an existing fleet of Windows or NAS boxes, nothing else matches its breadth.

Now the reasons it does not fit the core:

1. **License.** The mature SMB clients are copyleft. Samba and smbclient are GPL, libsmb2 and libdsm are LGPL (libsmb2 LGPL-2.1 confirmed against its `COPYING`). None can be relicensed MIT and none can sit statically inside an MIT core. Our License policy section rules them out of core on this point alone.

2. **Protocol impedance mismatch, this is the deep one.** SMB models a file as bytes plus a fixed, Windows-shaped metadata set. It is honest to admit SMB is not metadata-free: it has extended attributes and NTFS alternate data streams. But none of that maps cleanly onto what Haiku actually needs, namely typed BFS attributes keyed by `B_*_TYPE`, MIME semantics, and above all live queries. SMB has no notion of a live query at all. So even a flawless SMB client cannot deliver native mode. The headline feature of Campiello is precisely the thing SMB was never designed to carry.

3. **Weight and maintenance.** SMB is a large, still-evolving family (SMB1, 2, 3, dialect negotiation, signing, encryption, DFS, leasing). Owning a client, or vendoring Samba, is a heavy and permanent maintenance load. For interop we do not need to carry that, lighter permissive protocols cover the real need (SFTP via libssh2, WebDAV, and the in-tree NFS4).

4. **Discovery legacy.** SMB's classic discovery is NetBIOS browsing with a master browser, which is exactly the brittle mechanism that made the original WON feel fragile. Modern SMB moved to WS-Discovery, but that is still heavier and less clean than mDNS/DNS-SD. Adopting SMB would not even hand us good discovery for free.

5. **Security model mismatch.** SMB authentication is enterprise-shaped: NTLM, Kerberos, Active Directory domains. That is the opposite of the invisible, one tap, zero-config security in the Experience requirements section. Pulling in SMB auth would import a heavy stack that actively fights the experience goals.

Conclusion and where SMB still lives. SMB stays available for the one job it is genuinely best at: reaching an existing Windows or NAS box that offers nothing but SMB. It is kept as an **optional, dynamically loaded, LGPL add-on under `optional/smb/`**, off in the default build, isolated from the MIT core. Interop mode prefers SFTP, WebDAV, and NFS4. Native mode is CNP. This way Campiello keeps SMB's reach as a fallback without paying its license, weight, discovery, and security costs in the core, and without letting it cap the ceiling of what native mode can do.

---

## 7. Architecture

Three layers, each replaceable, mapped to Venetian names (suggestions, rename freely).

```
                 Tracker  +  Deskbar replicant (peer presence)
                                  |
        +-------------------------+-------------------------+
        |                                                   |
   Fondamenta                                            Bricola
   (userlandfs module, the canal-side                   (discovery daemon,
    foundation Tracker walks on)                          the mooring posts
        |                                                  that guide peers)
        |  one mount, backend selected per peer                 ^
        |                                                       | mDNS / DNS-SD
   +----+-----------------------------+                         |
   |                                  |                         |
 Interop backends                Native backend                |
 (SFTP/libssh2, WebDAV, NFS4)     Traghetto (CNP client) <------+
                                  the ferry that carries
                                  data across the canal
```

- **Fondamenta**: the userlandfs add-on. Presents remote peers as mountable volumes in Tracker. Implements the libfuse 2.x `struct fuse_operations` subset. Chooses a backend per peer (interop or native).
- **Traghetto**: the native transport and protocol (Campiello Native Protocol, CNP). TLS over TCP now, QUIC later.
- **Bricola**: the discovery daemon. Advertises and browses `_campiello._tcp` over mDNS/DNS-SD. Feeds a Deskbar replicant so peers appear and disappear live.
- **Bossolo** (optional second surface, see the Bossolo section): BMessage transfer over the LAN, riding the same Traghetto transport, Bricola discovery, and trust layer. Not routed through userlandfs. This is content sharing (dragged text, images, refs), not file mounting, and it is sequenced after the core file path.

The file path (Fondamenta over Traghetto) is the core and ships first. Bossolo is a sibling surface on the same plumbing, added later, never a precondition for the double click in Tracker.

---

## 8. Fondamenta: userlandfs module

**Two userlandfs front ends, one backend abstraction (decision C, M0-verified).** M0
reading of the source (see `docs/VERIFIED.md` section 1 and Risks item 1) found that the
userlandfs FUSE bridge is read-only on attributes and flattens every non-MIME attribute
to `B_RAW_TYPE` (`FUSEVolume.cpp:2495-2497,355-359`). It therefore cannot carry the typed
BFS attributes that are native mode's headline feature. The native userlandfs interface,
by contrast, exposes typed `CreateAttr(..., uint32 type, ...)` + `WriteAttr`
(`server/Volume.h:138,148`), with the type code preserved end to end. So Fondamenta uses
the right front end per mode:

- **Interop mode -> libfuse 2.x front end** (`struct fuse_operations`). Interop peers
  (SFTP/WebDAV/NFS4) have no Haiku typed attributes to preserve, so the read-only/untyped
  limitation does not bite. This is also the fastest way to the first Tracker "wow" (M1).
- **Native mode (Haiku-to-Haiku) -> native userlandfs front end.** Preferred form: a
  standard Haiku `file_system_module_info` / `fs_vnode_ops` built against the public,
  shipped header `headers/os/drivers/fs_interface.h`, loaded through the installed
  `libuserlandfs_haiku_kernel.so`. This gets typed attribute write without depending on
  userlandfs's unshipped private C++ base-class headers.

Both front ends sit above one internal backend abstraction, so the protocol/transport
code is shared and only the userlandfs binding differs.

libfuse (interop) callback phases:
- Phase 1 (read-only browse): `getattr`, `readdir`, `open`, `read`, `release`, `readlink`, `statfs`.
- Phase 2 (write): `mkdir`, `rmdir`, `create`, `write`, `unlink`, `rename`, `truncate`, `utimens`, `chmod` (where meaningful on Haiku).
- (Attribute callbacks `getxattr`/`listxattr` are read-only through the bridge; `setxattr`/`removexattr` are not wired by the stock bridge, so interop does not round-trip Haiku attributes. This is expected and acceptable for interop peers.)

Native (fs_interface) operation set carries the full typed-attribute path:
`lookup`, `read_stat`, `open_dir`/`read_dir`, `open`/`read`, `read_symlink` (read);
`create`, `write`, `create_dir`, `unlink`, `remove_dir`, `rename` (write);
and the payoff: `open_attr_dir`/`read_attr_dir`, `create_attr` (with `uint32 type`),
`write_attr`, `read_attr`, `remove_attr`, plus optional index/query ops for live queries.
See the operation map in `docs/VERIFIED.md` section 1.

Backend interface (internal, our own C++ abstraction so interop and native reuse the same
protocol/transport code beneath whichever userlandfs front end is active):
```
class PeerBackend {
  virtual status_t Stat(const char* path, struct stat&, AttrSet* out) = 0;
  virtual status_t ReadDir(const char* path, DirSink&) = 0;
  virtual status_t Read(const char* path, off_t, void*, size_t&) = 0;
  virtual status_t Write(const char* path, off_t, const void*, size_t&) = 0;
  virtual status_t ReadAttrs(const char* path, AttrSet& out) = 0;   // native only, no-op on interop
  virtual status_t WriteAttrs(const char* path, const AttrSet&) = 0; // native only
  virtual status_t Query(const char* predicate, QuerySink&) = 0;     // native only
  // open/close/create/rename/unlink/mkdir...
};
```
- `SftpBackend` implements the common subset using libssh2, hosted by the libfuse front end. Attribute and query methods are no-ops.
- `CnpBackend` implements everything, including typed attributes and queries, hosted by the native fs_interface front end.

Build path (M0-verified): userlandfs dev headers ship out-of-tree at
`/boot/system/develop/headers/private/userlandfs/`; the FUSE front end links
`libuserlandfs_fuse.so`, and the native front end either links `<nogrist>userlandfs_server`
(C++ base classes) or ships an `fs_interface` module reusing the installed
`libuserlandfs_haiku_kernel.so`. Both require the `userland_fs` package as a runtime
dependency. Not present in the M0 environment, so mount + attribute round-trip must be
smoke-tested on a real system before the design is frozen (M1). Mount is the public C API
`fs_mount_volume(where, device, "userlandfs", flags, "<campiello_fs> <opts>")`.

---

## 9. Bricola: discovery

Goal: peers appear by themselves, the WON effect, with no NetBIOS, no master browser, no manual IP entry.

Mechanism: mDNS / DNS-SD.
- Service type: `_campiello._tcp`.
- TXT record keys: `v` (protocol version), `node` (friendly name), `caps` (capability flags), `bfs` (1 if native BFS attribute and query support), `port`.
- Browse to populate the peer list, resolve to get host and port, subscribe to add and remove events for live presence.

License-driven choice for the responder:
- Avahi is LGPL, so it is out of core.
- Apple's mDNSResponder is Apache-2.0, acceptable as an optional dependency **if** a packaged HaikuPort exists. To verify: check HaikuPorts for an `mDNSResponder` or equivalent recipe.
- MIT-clean default recommendation: ship a **minimal embedded DNS-SD responder** (advertise plus browse for one service type only). This is a bounded, well-specified piece of work (RFC 6762 mDNS, RFC 6763 DNS-SD), keeps the whole core MIT, and removes the external dependency. Scope it to exactly what Campiello needs, not a general purpose responder.

Deskbar integration: a replicant showing online peers, reusing patterns already proven in Dogana (connection monitoring) and LANterna (LAN scanning). The scan and presence logic from LANterna is a natural feeder here.

Discovery is unauthenticated, treat it as untrusted:
- mDNS advertisements are spoofable. Any node can claim any `node` name, including impersonating a machine you know. Discovery only proposes candidates, it never confers trust.
- Trust comes only from the pairing step and the pinned key, never from the advertised name. The friendly name shown in the pairing prompt is attacker-controllable, so it is treated as a label, not an identity. Pin to the key, not the name.
- After pinning, a peer is recognized by its key. A familiar name presenting a different key is a stranger, and re-raises the pairing prompt rather than being trusted.

---

## 10. Traghetto: Campiello Native Protocol (CNP)

This is the novel layer. It is ours, so it is MIT, and it can express things SMB and NFS cannot.

Transport:
- TLS 1.3 over TCP for the first implementation. Always on, never optional.
- QUIC as a later option for multiplexed streams and faster setup.
- Mutual authentication between trusted nodes.

Trust model (strong but transparent, this is a first-class requirement, see the Experience requirements section):
- Each node generates a long-lived identity keypair on install. The user never sees, types, or manages it.
- Trust on first use with explicit consent: the first connection attempt raises a one tap "Allow NomePC to connect?" prompt on the target. On accept, the peer's public key is pinned. This is the LocalSend pattern, no CA, no certificate UI.
- After pinning, subsequent connections are silent and authenticated against the pinned key. A changed key re-triggers the prompt, which surfaces impersonation as a visible event rather than a silent failure.
- Revocation is one click in the Deskbar replicant. No key files to edit.
- Decide in M2 only the mechanics under the hood (key storage location, pinning store format), not whether to expose any of it. None of it is exposed.
- The default shared scope is a single "Condivisa" folder, not the whole disk. Widening scope is an explicit opt in.

Framing (first cut, revise during M2):
```
+--------+--------+--------+------------+------------------+
| magic  | ver    | type   | request_id | payload_len (u32)|  payload (CBOR) ...
| 2 bytes| 1 byte | 1 byte | 4 bytes    | 4 bytes          |
+--------+--------+--------+------------+------------------+
```
- Payload encoded as CBOR for compactness and easy evolution.
- `request_id` allows pipelining and out-of-order replies.

Message types (initial set):
- `HELLO` and `WELCOME`: capability handshake, version negotiation, node identity.
- `LIST`: directory listing, each entry carries name, stat, and the full attribute set.
- `STAT`: single entry, full attributes.
- `OPEN`, `READ`, `WRITE`, `CLOSE`: file IO.
- `MKDIR`, `UNLINK`, `RENAME`, `TRUNCATE`.
- `READ_ATTRS`, `WRITE_ATTRS`, `LIST_ATTRS`: BFS extended attributes, 1 to 1 fidelity.
- `QUERY_OPEN`, `QUERY_RESULT`, `QUERY_UPDATE`, `QUERY_CLOSE`: distributed live queries.

Attribute fidelity:
- Each attribute is carried as `{ name, type, size, bytes }` where `type` is the Haiku `B_*_TYPE` code. The receiver writes it back verbatim, so a file copied Campiello to Campiello keeps its MIME type, icon, ratings, and any app-specific attributes exactly. Verify the type codes and attribute IO against `headers/os/storage/fs_attr.h` and the `BNode` attribute methods before implementing.

Distributed live query (the headline feature):
- Client sends `QUERY_OPEN` with a Haiku query predicate string.
- Each peer translates it into a local `BQuery`, streams matches back as `QUERY_RESULT` messages as they arrive, then keeps the query live and pushes `QUERY_UPDATE` for entries added or removed, mirroring Haiku live query semantics.
- Fondamenta can expose a virtual folder per query so a single Tracker window shows live results aggregated from several machines. Verify `BQuery` and live query behavior against `headers/os/storage/Query.h` and the `BVolumeRoster` and `BVolume` APIs.

Security note: a malicious or buggy peer must never be able to escape the shared root or smuggle attributes that corrupt the local index. Path canonicalization and attribute validation are required on both ends. Treat every peer as untrusted input.

---

## 11. Bossolo: BMessage transfer over the LAN (optional second surface)

Working name **Bossolo** (the small container used in old Venetian voting to pass items around). Rename freely. This surface is sequenced after the core file path and is never a precondition for it.

### The idea and why it fits Haiku
On Haiku the `BMessage` is the universal currency of IPC and of drag and drop. When you drag text, an image, or other content, the negotiated payload is already a typed `BMessage` (`B_SIMPLE_DATA` and friends). A `BMessage` flattens to an architecture-neutral byte sequence and unflattens losslessly, so sending one across the LAN is natural. This generalizes Campiello from file sharing to content sharing: dragged text, images, and refs can travel between Haiku machines, something SMB and NFS cannot express.

This is not a new paradigm, it is a revival. MUSCLE's Message, the basis of BeShare, was modeled on `BMessage` precisely to move typed, nested, flattenable messages across a network. BeShare also pioneered live queries and attribute-aware listings over the wire. Campiello brings those ideas back, peer to peer, with no central server, woven into the desktop. BeShare is public domain and MUSCLE is permissive, so both are free to study as reference (verify MUSCLE's exact license before reusing any code, the preferred path is building native on `BMessage` directly).

### Surfaces and flow
- **Send gesture**: drag content onto a peer in the Deskbar replicant. No separate transfer app, consistent with the Tracker-native rule.
- **Wire**: a new CNP message type `DELIVER_MESSAGE` carries a flattened `BMessage` plus minimal metadata (sender node, declared payload kind).
- **Receive**: arrives as a `BNotification` ("NomePC sent you something"). On accept, the content lands on the clipboard, or as a file on the Desktop, or in a small inbox folder. This follows the consent-first model in the Experience requirements section.

### Hard constraints (these are not optional)
1. **Incoming BMessages are inert data by default.** Never dispatch a remote `BMessage` to `be_app`, a `BMessenger`, or via `BRoster`. Doing so would build a remote control and code-execution surface. Only a whitelist of payload kinds is ever surfaced: plain text, image/bitmap data, and file refs that arrive together with their transferred bytes. Everything else is dropped, not delivered. This extends the "treat peers as untrusted" rule.
2. **`entry_ref` does not travel.** Refs point at the sender's paths and are meaningless on the receiver. For dragged files, detect refs and transfer the actual bytes (route through the file path), or strip them. Never resolve a remote ref against the local filesystem.
3. **Remote drop injection is out of scope.** Synthesizing a drop into a specific running app on another machine is not done. The supported outcomes are notification then paste, Desktop drop, or inbox. Promise only what holds.

### Haiku APIs involved (verify against headers before use, do not assume signatures)
- `BMessage::Flatten` / `BMessage::Unflatten` and the flattened format, in `headers/os/app/Message.h`.
- The negotiated drag and drop message format (`B_SIMPLE_DATA`, the `be:` field name conventions). Verify against the Haiku drag and drop documentation and the relevant interface kit headers, do not guess field names.
- `be_clipboard` and `BClipboard`, in `headers/os/app/Clipboard.h`, for the paste outcome.
- `BNotification`, in `headers/os/app/Notification.h`, for the receive prompt.

### Build and packaging
Bossolo is a standard Haiku app plus a small daemon piece, sharing Traghetto, Bricola, and the trust store. It ships in the same hpkg, off-path from the file mounting, and adds no dependency beyond what the core already links.

---

## 12. Sharing, permissions, and write semantics

Product-level policy for what a paired peer may see and do. Design intent, firmed up alongside M3.

Sharing model:
- Each node exposes one shared root by default, the "Condivisa" folder, not the whole disk. Exact location to confirm (a per-user folder under home is the likely choice, verify against Haiku's `find_directory` conventions).
- Additional shared folders are an explicit opt in, added one by one, never automatic.

Permissions:
- Access is per paired peer. Pairing grants access to the shared root and nothing outside it.
- Two levels per shared folder, read-only or read-write, chosen by the owner, default read-only. A newly paired peer starts read-only until the owner grants write.
- No finer per-file or per-user matrix in this cycle (see Scope and non-goals). Simplicity is the feature.
- The shared root is a hard boundary. Path canonicalization on both ends, no symlink or ref escape above the root.

Write semantics:
- Last write wins at file granularity. No distributed locking in the first cycle.
- No partial-file merge. A write replaces bytes at the given offset, and concurrent writers to the same file are not protected beyond last write wins. State this plainly so no one expects transactional behavior.
- Renames and deletes inside the shared root are honored, subject to the read-write permission.
- Attribute writes follow the same permission and boundary rules as data writes.

---

## 13. Error handling and reconnection

Networks drop. The tool must degrade gracefully, never hang Tracker, and never expose raw internals. Design intent.

Peer lifecycle:
- A peer that goes offline disappears from the replicant and its mounted volume becomes unavailable rather than frozen. Tracker operations against it fail fast with a friendly error, not an indefinite spin.
- On the peer's return, discovery re-adds it and the volume reconnects using the pinned key, silently, with no re-pairing.

In-flight failures:
- A transfer interrupted mid-file fails cleanly. No half-written file is presented as complete. Prefer write-to-temp then atomic rename on the receiver where the backend allows it.
- A live query whose peer drops removes that peer's contributions from the aggregated view and marks the source offline, without tearing down the other peers' results.

Timeouts and backoff:
- Connect and request timeouts are bounded so Tracker never blocks indefinitely.
- Reconnection uses capped exponential backoff, so a flapping peer does not busy-loop the daemon.

Pairing and key change:
- A rejected pairing prompt denies access quietly, and inbound pairing attempts are rate limited so a hostile peer cannot storm the prompt.
- A changed peer key re-raises the pairing prompt and is never silently trusted, surfacing possible impersonation as a visible decision.

Error surface rule:
- User-visible errors are short and human ("NomePC is offline"), never a protocol code or a stack detail. Internals go to a developer log, not to the user.

---

## 14. Caching, concurrency, and performance

Tracker generates bursts of stat and readdir calls, so a naive one-request-per-syscall design will feel slow. Design intent, to validate under load early, tied to the userlandfs risk.

Caching:
- Cache directory listings and attributes for a short, bounded TTL, invalidated by native-mode change notifications where available. Interop backends without change notification rely on the TTL alone.
- Read-ahead for sequential reads to hide round-trip latency.
- Bound cache memory and evict least-recently-used. No unbounded growth.

Concurrency:
- The Traghetto daemon serves multiple peers concurrently. Model to confirm in M2, a small thread pool or an async event loop, not one unbounded thread per connection.
- Live query fan-out runs so a slow or flooding peer cannot stall the others. Per-peer queues with backpressure, plus the result quotas already noted in Risks.
- userlandfs request handling must not block the whole volume on one slow peer operation.

Performance intent (targets set concretely during M2):
- Browsing a shared folder feels local for small directories.
- A large directory streams progressively rather than blocking until fully listed.

---

## 15. Testing strategy

A two-node system needs a way to test both halves without two physical machines. Design intent.

- **Loopback and mock peer.** A CNP server and client on one machine over loopback, plus a mock peer that speaks the wire format, so protocol logic is testable in CI without special hardware.
- **Two-VM integration.** A pair of Haiku VMs on a virtual network for the real discovery, pairing, mount, and transfer path. This is the honest end to end test, since mDNS and userlandfs cannot be fully faked.
- **Wire format conformance.** Golden encode and decode tests for every CNP message type, so protocol changes that break compatibility are caught. Keep these in sync with `docs/PROTOCOL.md`.
- **Fuzzing the untrusted surface.** Feed malformed frames, oversized payloads, path-escape attempts, and hostile BMessages into the receive paths. The "treat peers as untrusted" rule is only real if it is tested.
- **Attribute fidelity round-trip.** Automated check that a file copied native to native keeps its MIME type and attributes byte for byte.
- **Experience gate checks.** The naive-user acceptance tests in the milestones stay manual, but scripted setup (fresh install, second node) makes them repeatable.

---

## 16. Repository layout

```
campiello/
  README.md
  LICENSE                      # MIT
  docs/
    PROPOSAL.md                # this document
    PROTOCOL.md                # CNP wire spec, kept authoritative
    VERIFIED.md                # the Verified ground truth facts, with the header paths checked
  src/
    fondamenta/                # userlandfs module (libfuse 2.x front end)
      backend/
        PeerBackend.h
        SftpBackend.*          # libssh2, BSD
        CnpBackend.*           # native
    traghetto/                 # CNP client and server libraries
      wire/                    # framing, CBOR codec
      tls/                     # mbedTLS or Haiku SecureSocket wrapper
      server/                  # the daemon serving local files and queries
    bricola/                   # discovery
      mdns/                    # minimal DNS-SD responder (MIT) OR mDNSResponder glue
      replicant/               # Deskbar peer presence replicant
    bossolo/                   # BMessage transfer surface (see Bossolo section), shares traghetto
      send/                    # drag-onto-peer gesture, flatten and DELIVER_MESSAGE
      receive/                 # notification, consent, clipboard/Desktop/inbox landing
  optional/
    smb/                       # LGPL, dynamically loaded, never in default build
  tests/
```

---

## 17. Build system

To confirm in M0, do not assume:
- Whether Fondamenta builds in-tree (added to the Haiku build) or out-of-tree against a userlandfs SDK package.
- Which TLS backend Haiku's `SecureSocket` links, to decide between reusing it or vendoring mbedTLS.

Default plan pending that confirmation:
- Traghetto and Bricola as standard Haiku apps and libraries, plain Makefile or Jam, linking Haiku kits and libssh2 (from HaikuPorts) and mbedTLS (from HaikuPorts).
- Fondamenta against the userlandfs build path confirmed in M0.

---

## 18. Milestones

- **M0 Verification spike (no product code).** Confirm the open items: userlandfs out-of-tree build path, FUSE xattr to Haiku attribute mapping, HaikuPorts availability of libssh2, mbedTLS, and any mDNSResponder, and the TLS backend behind SecureSocket. Output: `docs/VERIFIED.md` updated, build skeleton that compiles an empty userlandfs add-on and mounts an empty volume in Tracker.
- **M1 Interop read-only.** SftpBackend plus Fondamenta read path. A remote SFTP host appears as a folder in Tracker and you can browse and read files. This is the first visible WON moment.
  - Experience gate: ships as a single installable hpkg. After install, the mount appears with no terminal step. (Discovery and pairing land in M2, so M1 may still require entering a host once, which M2 removes.)
- **M2 Native core plus zero-config and pairing.** CNP handshake, LIST, STAT, READ over TLS. CnpBackend read path. Bricola discovery live, so two Haiku boxes see each other by themselves. First connection raises the one tap "Allow NomePC?" prompt, then trust is pinned and silent. Protocol frozen enough to write `PROTOCOL.md`.
  - Experience gate (the headline "easy" test): a person who has never seen Campiello, with no instructions, installs the hpkg, sees the other machine appear by itself, taps allow once, opens the peer in Tracker, and reads a file. No spoken or written instruction at any step. TLS is always on, and no key, certificate, or CA ever appears in the UI.
- **M3 Attributes and write.** Extended attribute fidelity end to end, plus write operations in both backends where supported. Copy a file Campiello to Campiello and confirm MIME type and attributes survive.
  - Experience gate: default shared scope is the single "Condivisa" folder. Widening scope is an explicit opt in, never the default.
  - Applies the Sharing, permissions, and write semantics section: read-only default per peer, last write wins, shared-root boundary enforced on both ends.
- **M4 Distributed live query.** QUERY messages, per-query virtual folders, live updates. A single Tracker window shows live results from several machines. This is the feature no other OS has.
- **M5 Polish.** Deskbar replicant presence and one click revoke, reconnection, mount management UI, error states that stay friendly and never expose internals.
  - Experience gate: revoking a paired peer is one click in the replicant, with no key management.
  - Implements the Error handling and reconnection section: fail-fast on offline peers, capped backoff, human-only error text.
- **M6 Bossolo, BMessage transfer (optional surface, see the Bossolo section).** `DELIVER_MESSAGE` over the existing transport. Drag content onto a peer in the replicant, it arrives on the other machine as a consent notification, then lands on the clipboard or Desktop. Whitelist of payload kinds enforced, incoming messages inert by default, no dispatch to apps, refs only with transferred bytes.
  - Experience gate: drag selected text onto a peer, accept once on the other side, paste it there. No app launch, no key, no instruction needed.
  - Sequencing: starts only after M3, so the core file path is solid first. Never blocks M1 to M5.

---

## 19. Working agreement

This project has specific guardrails. Follow them on every task.

1. **Verify, do not recall.** Before using any Haiku, BFS, FUSE, or kit API, read its header in the actual Haiku source and match the real signature. Never invent a function, struct field, or constant. If a needed API cannot be located, stop and say so rather than guessing.
2. **MIT-only in core.** Any new dependency must be MIT, BSD, Apache-2.0, ISC, zlib, or public domain. LGPL and GPL code goes only under `optional/`, dynamically loaded, never in the default build. If a task seems to require an LGPL or GPL library in core, stop and raise it.
3. **Propose before large code.** For any non-trivial component, write the interface and a short design note first, get a yes, then implement. Small, reviewable commits over big drops.
4. **English in code and dev docs.** User-facing strings that ship to end users are Italian. Internal code, comments, commit messages, and these docs are English.
5. **No em dashes** anywhere in output.
6. **Keep `docs/PROTOCOL.md` and `docs/VERIFIED.md` authoritative.** When the wire format or a verified fact changes, update the doc in the same commit.
7. **Treat peers as untrusted.** Validate paths and attributes on both ends. No path escape, no unchecked attribute writes.
8. **Experience first.** Easy install, zero-config first use, and invisible security (see Experience requirements) outrank feature breadth. Never add a terminal step, a config file, or a visible key, certificate, or CA to the default path. If a feature needs one, put it behind a default or an advanced toggle, and raise it.

These guardrails are also summarised for contributors in `CONTRIBUTING.md` at the repository root.

---

## 20. Open questions to close in M0

1. userlandfs out-of-tree build: SDK package, or must Fondamenta build inside the Haiku tree.
2. Exact mapping from FUSE `getxattr`/`setxattr`/`listxattr` to Haiku native attributes inside the userlandfs FUSE bridge.
3. HaikuPorts availability and current versions of libssh2 and mbedTLS, and whether any mDNSResponder port exists.
4. TLS backend behind Haiku `SecureSocket`, to decide reuse vs vendoring mbedTLS.
5. Trust model under the hood only: key storage location and pinning store format. The user-facing model is already decided (trust on first use, one tap allow, pinned key, no CA, no visible key). The open part is implementation, not UX.
6. Whether the older in-tree `nfs` and `nfs4` clients are good enough to expose through Campiello's UI directly, or whether interop mode should wrap them.
7. The exact negotiated drag and drop `BMessage` format (`B_SIMPLE_DATA`, `be:` field names) needed by Bossolo. Verify against Haiku docs and headers before M6, do not guess.
8. MUSCLE's exact license, if any of its code is to be referenced or reused. The preferred path is building native on `BMessage`, so this only matters if reuse is considered.
9. Concrete location of the "Condivisa" shared root via `find_directory`, and whether the trust store and shared-folder config live per user or system wide.
10. Daemon concurrency model (thread pool vs async event loop) and the cache TTL and eviction policy, both decided in M2.

---

## 21. Risks

- **userlandfs performance and stability under load.** A userspace filesystem mounted in Tracker must stay responsive. Budget time for IO request handling and caching, and test with large directories and live queries early.
- **Writing an mDNS responder, even minimal, is real work.** If a clean Apache-2.0 mDNSResponder port exists, using it may be the better tradeoff than maintaining our own. Decide in M0 with the license and maintenance cost in view.
- **Live distributed query is novel and can get heavy.** Fan-out plus live updates across several peers needs backpressure and limits, or a busy LAN floods the client. Design quotas from the start.
- **Attribute fidelity edge cases.** Some attributes are app-private and large. Define size limits and a policy for attributes that fail to apply on the receiver.
- **Reconciling zero-config with consent.** Auto-discovery plus a single allow tap is the whole bet on "easy and safe". The danger is prompt fatigue (too many prompts trains users to tap allow blindly) or prompts that leak technical detail. Keep prompts rare, human, and key-free, and test the first-run flow on someone unfamiliar early, not at M5.
- **Bossolo is an attack surface and a scope risk.** Receiving remote BMessages is powerful and dangerous. The inert-by-default rule, the payload whitelist, and the no-dispatch rule are mandatory, not nice to have. As a feature it is also tempting to pull forward, do not let it slip ahead of the core file path, it is M6 for a reason.

---

## 22. Why this is more than nostalgia

WON made Windows shares appear in Tracker. Interop mode reaches parity with that, license-clean. Native mode goes past it: a file is not flattened to bytes and a name, it keeps its full Haiku identity across the wire, and a query is not a local act but a lagoon-wide one. That is the BeOS "media OS" idea extended into a network-native OS, and it is the part of Campiello that no SMB or NFS stack can copy, because they were never designed to carry it.
