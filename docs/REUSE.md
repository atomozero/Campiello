# Reuse: harvesting from existing Haiku projects

What Campiello can lift from four sibling projects by the same author, and what it
must build fresh. Every claim below was checked by reading the referenced source on
2026-07-03. Line numbers are from that reading; re-verify if a tree moves.

Projects studied (all under `/Magazzino/`):

| Project | Role for Campiello | License |
| --- | --- | --- |
| `LocalSend` | pairing UX, transport, discovery, replicant | MIT (c) 2026 atomozero |
| `LANterna` | mDNS wire codec, discovery, replicant, presence | MIT (c) 2026 atomozero |
| `Sotoportego` | daemon architecture, settings/secret storage | MIT (c) 2026 atomozero |
| `Dogana` | presence diff, non-blocking probe worker | no LICENSE file (see below) |

Three of the four are MIT by the same author, so provenance is clean and code may be
lifted with attribution. **`Dogana` has no LICENSE file anywhere in its tree.** Since
the author is the same person, this is a paperwork fix, not a blocker: add an MIT
LICENSE to Dogana (or copy the pattern by hand) before lifting code verbatim.

---

## Corrections to the proposal

The proposal was written from design intent. Reading the source corrected three points.
These should also land in `PROPOSAL.md` when it is next revised.

1. **Dogana has no reconnection or backoff logic at all.** Section 13 and the milestone
   notes credit Dogana with "backoff patterns." Dogana is a passive poller of the kernel
   socket table with a flat refresh interval; it never opens or reconnects a socket.
   Capped exponential backoff is greenfield for Campiello. Dogana still contributes a
   presence-diff model and a non-blocking probe-worker pattern (below).

2. **LocalSend already proves OpenSSL works on Haiku, and OpenSSL is core-legal.**
   LocalSend links OpenSSL 3 (`-lssl -lcrypto`) for its self-signed TLS. OpenSSL 3 is
   Apache-2.0, which is on the allowed list in section 5. So the transport TLS decision
   (open question #4) has two proven-adjacent options, not one: bundle mbedTLS
   (Apache-2.0), or reuse the OpenSSL that LocalSend already drives. Haiku's
   `BSecureSocket` is itself OpenSSL-backed but neither LocalSend nor Sotoportego
   exercised it, so "does `BSecureSocket` do TLS 1.3 mutual auth + pinning cleanly" stays
   an unproven M0 spike. See the TLS decision note below.

3. **No existing project implements peer authentication.** LocalSend runs
   `SSL_VERIFY_NONE` and trusts a JSON-claimed fingerprint; its identity keypair is
   regenerated on every launch and never written to disk. Sotoportego delegates all TLS
   to a spawned `openvpn` process. So the entire trust core (persistent identity key +
   real certificate pinning + trust-on-first-use) is greenfield. Everything *around* it
   (accept prompt, transport plumbing, discovery, tray) is harvestable.

---

## By Campiello component

### Bricola (discovery + Deskbar replicant)

| Piece | Source | Verdict |
| --- | --- | --- |
| mDNS/DNS-SD wire codec: `EncodeDnsName`, `DecodeDnsName` (RFC 1035 compression pointers), `ParseResponse` for A/PTR/SRV | `LANterna/src/enrich/MdnsEnricher.cpp:19-189` | **reuse directly**, then add TXT parsing + the responder side |
| Raw-UDP multicast socket setup to 224.0.0.251:5353 (`SO_REUSEADDR`, `IP_MULTICAST_TTL=1`) | `LANterna/src/enrich/MdnsEnricher.cpp:196-227` | **reuse pattern**; add `IP_ADD_MEMBERSHIP` for a persistent listener |
| Multicast discovery: periodic announce + unicast reply + stale prune, TTL=1 | `LocalSend/src/net/MulticastAnnouncer.cpp`, `LocalSend/src/protocol/Constants.h:8-9` | **reuse directly** (swap group/port/fields; note this is a simpler alternative to full mDNS if the embedded responder slips) |
| Deskbar replicant skeleton: `Instantiate`/`Archive`, the `add_on`=own-signature reload trick, dual export entry points, HVIF-from-MIME icon, install via `BDeskbar::AddItem` | `LocalSend/src/replicant/DeskbarReplicant.cpp`, `LocalSend/src/app/DeskbarItem.cpp:33-90` | **reuse directly** (the only *complete, correct* Deskbar replicant of the four) |
| Replicant `Archive`/`Instantiate` boilerplate + "receive BMessage -> update field -> Invalidate" live-refresh idiom | `LANterna/src/ui/NetworkReplicant.cpp:42-108` | **reuse pattern** (skeleton only; it is a Desktop-dragger, never wired up, and never installed into Deskbar) |
| Worker-thread -> `BMessenger` -> window BMessage update protocol (correct "worker never touches UI" idiom) | `LANterna/src/ui/ScanRunner.cpp`, `LANterna/src/ui/Messages.h` | **reuse pattern**; this is how the Bricola daemon should feed the replicant live |
| OUI vendor lookup + bundled `oui.txt` for peer vendor labels | `LANterna/src/enrich/OuiDatabase.cpp`, `LANterna/oui.txt` | **reuse directly** (optional nicety) |
| mDNS-service -> friendly-name/type inference | `LANterna/src/enrich/MdnsEnricher.cpp:370-393` | **reuse pattern** for peer labels |

**What LANterna does NOT give us: the mDNS responder.** Its mDNS is a one-shot
querier/browser only. It never joins the multicast group persistently, never answers
incoming queries, never advertises `_campiello._tcp`, parses no TXT records, and has no
probing/conflict/goodbye handling. Bricola inherits the hard wire-format machinery and
must build the responder around it (RFC 6762 + 6763). This confirms the section 21 risk.

### Traghetto (native protocol daemon + TLS transport)

| Piece | Source | Verdict |
| --- | --- | --- |
| Daemon skeleton: `BApplication`/`BLooper` single event loop, `BHandler` backend interface (`Connect/Disconnect/State/SetObserver`), observer/broadcast fan-out with dead-client pruning | `Sotoportego/src/server/SotoportegoServer.{h,cpp}`, `Sotoportego/src/backend/VPNBackend.h` | **reuse pattern**; graft a real TCP `accept()` loop on top (Sotoportego's "clients" are local BMessage front-ends, not network peers) |
| Worker-thread does blocking I/O, posts parsed events back to the looper via `BMessenger(this).SendMessage` | `Sotoportego/src/backend/OpenVPNBackend.cpp:729-864` | **reuse pattern**; one reader thread per peer, posting frames to the looper |
| Non-blocking probe worker: `spawn_thread` + sem-signalled mutex queue + instant cache-returning API + **negative caching of failures** + atomic-flag/sem clean shutdown | `Dogana/src/core/DnsResolver.{h,cpp}` | **reuse directly as a template**; swap `getnameinfo()` for a connect-probe. This is the blueprint for "never hang Tracker, fail fast on offline peers" |
| Incremental stream-framing parser: `Feed(chunk)` -> buffer -> emit complete units, keep partial tail; plus partial-send resume and injection-safe arg escaping | `Sotoportego/src/backend/OpenVPNManagement.cpp:218-233`, `Sotoportego/src/backend/OpenVPNBackend.cpp:59-90, 962-994` | **reuse pattern**; swap "find `\n`" for "read 8-byte header then u32-len bytes" to de-chunk the CNP frame |
| Plain/TLS unified socket transport via `ReadFn`/`WriteFn` lambdas so one code path serves both | `LocalSend/src/net/haiku/SocketHttpServer.cpp:52-102`, `LocalSend/src/net/haiku/SocketHttpClient.cpp` | **reuse pattern**; add peer-cert extraction + pinning check at the `SSL_accept`/`SSL_connect` points |
| `common/` + protocol-header + daemon-owns-transport module split (GUI/CLI link only `common/`, never the backend) | `Sotoportego/src/common/`, `Sotoportego/src/*/Makefile` | **reuse directly** as the repo layout shape |
| "Keep a GPL tool at arm's length as a spawned subprocess" boundary | `Sotoportego/src/backend/OpenVPNBackend.cpp:637` | **reuse pattern** (relevant to the `optional/smb/` add-on) |

**CBOR is not present in any project.** All IPC here is either BMessage Flatten/Unflatten
(in-process) or hand-rolled JSON/line-text. The `magic + ver + type + request_id +
u32 len + CBOR payload` frame from section 10 is greenfield; only the incremental
de-chunker *pattern* carries over.

### Trust layer (identity + pairing + pinning)

| Piece | Source | Verdict |
| --- | --- | --- |
| Blocking accept-prompt handshake: server thread blocks on a `std::condition_variable`, posts `kMsgIncoming` to the GUI, `BAlert` Accept/Reject, `cv.notify_one()`. The literal "Allow NomePC to connect?" one-tap flow | `LocalSend/src/app/main_gui.cpp:394-400, 1622-1640, 1890-1929` | **reuse directly** |
| Self-signed cert generation + `SHA256(DER cert)` hex fingerprint | `LocalSend/src/net/TlsContext.cpp:18-99` | **reuse pattern**; keep the crypto, **add on-disk persistence** (the missing half of TOFU) |
| Fingerprint allow-list store (`Favorites`, one-per-line flat file) | `LocalSend/src/app/main_gui.cpp:307-366` | **reuse pattern**; rebuild as a real pinning store keyed on the **TLS-verified** pubkey, with key-change re-prompt and one-click revoke |
| Secret storage via Haiku `BKeyStore`/`BPasswordKey` (purpose `B_KEY_PURPOSE_NETWORK`) | `Sotoportego/src/gui/MainWindow.cpp:1004-1047` | **reuse pattern**; candidate home for the node private identity key (note: system keystore stores blobs, may prompt to unlock) |
| Local control-socket auth: random 32-hex per-session password in a 0600 `mkstemp` file, required as first line | `Sotoportego/src/backend/OpenVPNBackend.cpp:474-509` | **reuse pattern** if Traghetto ever exposes a local control socket |

**Greenfield in the trust layer:** (a) *persistent* long-lived identity keys - LocalSend
regenerates per launch; (b) actual peer-certificate pinning/verification - LocalSend runs
`SSL_VERIFY_NONE` and trusts a self-reported JSON fingerprint, so it encrypts but never
authenticates. Pin to the key, never the advertised name (section 9).

### Presence + reconnection (section 13)

| Piece | Source | Verdict |
| --- | --- | --- |
| Snapshot-diff + debounce presence model: `{ageTicks, closing}` one-tick grace before declaring a peer gone, fresh-online highlight, stable hashable identity key across state changes | `Dogana/src/ui/MainWindow.cpp:287-359`, `Dogana/src/core/Connection.{h,cpp}` | **reuse pattern**; maps ~1:1 onto peer online/offline with anti-flap. Rewrite the "snapshot" to be peer-probe results, not kernel sockets |
| Transition-only online/offline log (append `"<ts>:online\|offline"`, dedup transitions) | `LANterna/src/model/DeviceHistory.cpp:60-92` | **reuse pattern** |
| `BMessageRunner` + `BMessenger(this)` periodic self-tick, interval adjustable via `SetInterval()` | `Dogana/src/ui/MainWindow.cpp:267-284` | **reuse pattern**; the natural attach point for a backoff schedule (feed `SetInterval` the next backoff delay) |
| Portable-core layering: pure-std `model`/`net`/`scan` with zero BeAPI, BeAPI only in `ui` | `LANterna/src/` (whole layout) | **reuse pattern**; keeps discovery/protocol logic testable off-Haiku (section 15) |

**Capped exponential backoff and reconnection are greenfield** (see correction #1).

### Fondamenta (userlandfs module)

No reference project touches userlandfs, FUSE, or BFS-attribute mounting. Fondamenta is
entirely greenfield. M0 resolved its shape (decision C, see `docs/VERIFIED.md` section 1):
a libfuse 2.x front end for interop mode, and a native `fs_interface` front end for native
mode (because the FUSE bridge is read-only and type-lossy on attributes). Nothing to
harvest from the reference projects here; the in-tree templates are userlandfs's own
`server/fuse/FUSEVolume.cpp` and `server/haiku/HaikuKernelVolume.cpp`.

### Bossolo (BMessage transfer)

No direct code to lift. LocalSend's file-streaming client (`PostFile`, 64 KB chunks with
progress callback, `LocalSend/src/net/haiku/SocketHttpClient.cpp:312-414`) is a reference
for chunked transfer once the file path is solid. Otherwise deferred to M6.

---

## Cross-cutting patterns worth adopting everywhere

- **Portable core / BeAPI shell split** (LANterna, Sotoportego). Keep protocol, wire
  codec, and discovery logic in pure-std C++; confine libbe to UI and persistence. Enables
  the loopback/mock-peer testing in section 15.
- **Crash-safe settings persistence**: flatten a `BMessage` to `<name>.tmp`, `Unset()` to
  flush, `rename()` over the real file, under `find_directory(B_USER_SETTINGS_DIRECTORY)/Campiello/`
  (`Sotoportego/src/server/ProfileStore.cpp:126-201`). **reuse directly** for node config,
  the pinning store, and shared-folder config. This also answers open question #9's
  storage mechanism (location still to confirm).
- **`find_directory`, never CWD-relative paths.** LocalSend's biggest anti-pattern is
  storing identity/settings at `./localsend_*`. Campiello must not copy that.
- **Non-blocking-worker discipline** (Dogana `DnsResolver`, LANterna `ScanRunner`,
  Sotoportego reader threads): blocking network I/O never runs on a looper/UI thread;
  results are marshalled back via `BMessenger`. Adopt uniformly so Tracker never stalls.

---

## TLS backend decision note (feeds open question #4)

Evidence gathered:

- **OpenSSL 3 (Apache-2.0)** demonstrably builds and links on Haiku and does TLS 1.3 - LocalSend uses it today. Apache-2.0 is core-legal. Downside: LocalSend only ever ran it
  in `SSL_VERIFY_NONE` mode, so mutual-auth + pinning with OpenSSL on Haiku is un-exercised
  (but well-trodden on other platforms).
- **mbedTLS (Apache-2.0)** gives version-pinned control over TLS 1.3, mutual auth
  (`mbedtls_ssl_conf_authmode(REQUIRED)`), and public-key pinning, decoupled from whatever
  OpenSSL the system ships. Not proven on Haiku by any project here; needs a build spike.
- **`BSecureSocket`** is OpenSSL-backed but its wrapper surface gives less control over the
  handshake and pinning, and no reference project exercised it. Treat as unproven risk.

Recommendation to resolve in M0: spike mbedTLS and OpenSSL side by side for a mutual-auth +
pinning handshake, and pick on control vs. dependency-weight. Do not assume `BSecureSocket`
suffices without proving the pinning path.

---

## Ranked shortlist (highest value first)

1. **LocalSend Deskbar replicant** (`src/replicant/`, `src/app/DeskbarItem.cpp`) - reuse
   directly. The only complete, correct Deskbar replicant; the Archive/`add_on`/auto-restore
   subtleties alone save real time.
2. **LANterna mDNS wire codec** (`src/enrich/MdnsEnricher.cpp:19-189`) - reuse directly. The
   hard, error-prone half of mDNS, portable and dependency-free. Add TXT + responder.
3. **Dogana `DnsResolver` worker template** (`src/core/DnsResolver.{h,cpp}`) - reuse
   directly. Blueprint for the fail-fast, never-block peer-reachability probe.
4. **LocalSend accept-prompt handshake** (`main_gui.cpp:394-400, 1622-1640, 1890-1929`) -
   reuse directly. The one-tap "Allow?" flow.
5. **Sotoportego daemon skeleton** (`SotoportegoServer`, `VPNBackend.h`) - reuse pattern.
   The looper + BHandler backend + worker-posts-to-looper shape for the Traghetto daemon.
6. **Sotoportego crash-safe persistence** (`ProfileStore.cpp:126-201`) - reuse directly for
   config, pinning store, shared-folder config.
7. **LocalSend multicast discovery** (`MulticastAnnouncer.cpp`) - reuse directly as a
   simpler fallback to a full mDNS responder.
8. **Dogana presence-diff + debounce** (`MainWindow.cpp:287-359`) - reuse pattern for peer
   online/offline with anti-flap.

## Greenfield (nothing to harvest, must build)

- Fondamenta: userlandfs module, FUSE 2.x front end, BFS xattr path (blocked on M0 spike).
- CNP wire protocol: the CBOR-framed message set (only the de-chunker *pattern* carries over).
- Persistent identity keypair + real certificate pinning + TOFU binding.
- The mDNS *responder/advertiser* for `_campiello._tcp` (TXT, probing, conflict, goodbye).
- Capped exponential backoff and silent reconnection.
- Distributed live query (no prior art anywhere).
