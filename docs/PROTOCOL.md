# PROTOCOL.md: Campiello Native Protocol (CNP)

Authoritative wire spec for native mode (Traghetto). This is a **draft seed**; the
framing and message set are frozen during M2 (see `PROPOSAL.md` section 18). When the
wire format changes, update this file in the same commit (working agreement rule 6),
and keep golden encode/decode tests in sync (`PROPOSAL.md` section 15).

Status: **partially implemented.** The frame envelope (header encode/decode + incremental
de-chunker), the CBOR codec primitives, the HELLO / WELCOME handshake, the STAT / LIST
schema with AttrSet, the ERROR reply, the OPEN / READ / CLOSE read path, and now the write
path schemas (WRITE, the MKDIR / UNLINK / RENAME / TRUNCATE namespace mutations, and the
READ_ATTRS / WRITE_ATTRS attribute messages) are coded and unit-tested in
`src/traghetto/wire/` and `tests/wire/`, and the read path travels over a
real socket both in plain TCP (`src/traghetto/transport/`) and over mutually-authenticated,
SPKI-pinned TLS 1.3 (`src/traghetto/tls/`), driven by a request/response dispatcher
(`src/traghetto/dispatch/`) and a multi-peer daemon (`src/traghetto/server/`), all
loopback-tested. The server-side handlers that carry out the write path are now implemented
too (`FileServer`: WRITE, the namespace mutations, and WRITE_ATTRS, with the per-peer
read-only default and shared-root enforcement on every mutation), and the client that drives
them (`CnpBackend`: OpenWrite/Write/Mkdir/Unlink/Rename/Truncate/WriteAttrs over PeerBackend,
loopback-tested end to end against a writable daemon). Still to write: the QUERY messages
(M4). No reference project uses
CBOR or this framing; see `docs/REUSE.md` (only the incremental stream de-chunker
*pattern* from Sotoportego carries over). The Haiku APIs this protocol maps onto
(attribute IO, type codes, BQuery live semantics, TLS backend) are M0-verified against
source; see `docs/VERIFIED.md`. The framing constants are provisional until frozen in M2.

---

## Transport

- TLS 1.3 over TCP. Always on, never optional.
- Mutual authentication between trusted nodes, pinned public keys (trust on first use).
- QUIC is a later option for multiplexed streams and faster setup.
- **TLS backend: DECIDED in M0.** Drive a TLS library directly; do NOT use Haiku's
  `BSecureSocket` (its public API offers no client-cert mutual auth, no TLS-version
  control, and no working key/cert pinning - verified, see `docs/VERIFIED.md` section 6).
  Primary backend: **system OpenSSL 3.5.6** (Apache-2.0, already on Haiku and linked by
  the sibling LocalSend). Self-contained alternative: **bundle mbedTLS 3.6.5**
  (Apache-2.0). Both do TLS 1.3 + mutual auth + public-key (SPKI) pinning.
- Pin to the peer's SubjectPublicKeyInfo hash (SHA-256 over `i2d_PUBKEY`), not the full
  cert and not the advertised name, so a re-issued cert with the same key stays trusted
  while a changed key re-raises the pairing prompt.

**Implemented so far:**
- A plain-TCP framed connection (`src/traghetto/transport/Connection.*`, `Send(Frame)` /
  `Receive(Frame)` over a socket, pure POSIX) with a loopback test.
- A node identity (`src/traghetto/tls/Identity.*`): an Ed25519 keypair + self-signed cert,
  with the SPKI fingerprint (SHA-256 of `i2d_PUBKEY`) that trust pins, and
  trust-on-first-use `LoadOrGenerate` persistence.
- The secure transport (`src/traghetto/tls/TlsConnection.*`): TLS 1.3, mutually
  authenticated, with SPKI pinning after the handshake (no CA). Loopback-tested: two
  identities pin each other, exchange an encrypted HELLO/WELCOME, and a wrong pin is
  refused. The CNP-layer `fp` in HELLO/WELCOME is expected to equal the pinned TLS
  fingerprint (verified in the test).

- The request/response dispatcher (`src/traghetto/dispatch/Dispatch.*`): `ServeConnection`
  (a server serve-loop over a `FrameChannel` that hands each request to a `RequestHandler`
  and echoes the reply's `request_id`) and `Client` (correlates replies by `request_id`,
  handling out-of-order). Transport-agnostic via the `FrameChannel` interface, which both
  `Connection` and `TlsConnection` implement. Tested over loopback: synchronous
  request/reply, an unhandled request answered with ERROR, and several clients served
  concurrently (one server thread per connection).

- The multi-peer daemon (`src/traghetto/server/Daemon.*`): an accept loop that wraps each
  connection via a `ChannelFactory` (`PlainChannelFactory` for tests, `TlsChannelFactory`
  for native mode) and serves it on a thread per peer with `ServeConnection`. `Stop()` shuts
  down cleanly, unblocking peer threads via `FrameChannel::Shutdown`. Tested: several
  concurrent clients over a plain daemon with a clean stop, and a pinned client over a TLS
  daemon.

- The server-side backend (`src/traghetto/server/FileServer.*`): a `RequestHandler` that
  serves a rooted local directory read-only over CNP - HELLO, STAT, LIST (entries with stat
  and typed BFS attributes), and OPEN/READ/CLOSE. Every peer path is resolved with
  `realpath` and required to stay within the shared root, so `..` and symlink escapes are
  refused; open handles are per-connection and bounded. Tested end to end (a CNP client
  browses and reads a real directory through the daemon, attributes and all guards).

- The client-side backend (`src/fondamenta/backend/`): `PeerBackend` is Fondamenta's
  portable internal interface (wire types, a `BackendStatus`), and `CnpBackend` implements
  it by turning `Stat`/`ReadDir`/`Open`/`Read`/`Close` (plus `Hello`) into CNP requests over
  a `FrameChannel`, decoding replies and mapping ERROR codes. Loopback-tested end to end: a
  `CnpBackend` browses and reads a real directory served by the daemon + `FileServer`,
  attributes and error mapping included.

- The trust-on-first-use store (`src/traghetto/trust/`): `TrustStore` is the persistent set
  of paired peers keyed by identity fingerprint - `Pin`/`Forget`/`IsTrusted` plus an
  `Evaluate` that returns kTrusted / kUnknown / kKeyChanged (a pinned name presenting a
  different key, surfaced as possible impersonation), with atomic 0600 file persistence.
  `Paths` resolves the per-node file locations via `find_directory`
  (`B_USER_SETTINGS_DIRECTORY/Campiello/`, with a `$HOME/.config` fallback off Haiku) for the
  identity and trust-store files. The `Fingerprint` type and its hex encoding were split into
  an OpenSSL-free `src/traghetto/tls/Fingerprint.*` so the trust layer uses them without the
  TLS stack. Portable, tested (`tests/trust/`): the trust decisions, save/load round-trip,
  malformed-line skipping, and the resolved paths.

- The pairing gate wiring the store into the CNP handshake: `Pairing`
  (`src/traghetto/trust/`) is the policy - given the peer's authenticated fingerprint and
  claimed name it consults the `TrustStore`, admits a pinned peer silently, and for an unknown
  or key-changed peer raises the one-tap consent through an abstract `PairingPrompt` seam
  (the Haiku `BAlert` implements it; the portable core stays testable). On allow it pins and
  persists. Prompts are rate-limited by a global monotonic `TokenBucket` (default: a burst of
  5, refilling one every 15s) consulted only on the unknown / key-changed path, so a hostile
  peer that reconnects in a loop is refused quietly without barraging the user, while a
  trusted peer reconnecting is never throttled (docs/PROPOSAL.md section 12). `GatedHandler`
  (`src/traghetto/server/`) is a `RequestHandler` decorator that enforces this before any
  request reaches the `FileServer`: the first message must be HELLO, the identity it claims
  must equal the fingerprint TLS authenticated, and until the peer is admitted every request
  is refused with ACCESS_DENIED. The peer fingerprint now reaches the handler via
  `FrameChannel::PeerFingerprint()` and `HandlerFactory::Create(peer)`. Tested with a fake
  prompt and an injected clock: silent-for-trusted, allow/deny, the fingerprint-mismatch and
  must-HELLO-first refusals, prompt rate-limiting and its refill, and pin persistence
  (`tests/trust/test_pairing`, `tests/server/test_gatedhandler`).

- The Haiku implementation of the prompt (`src/traghetto/server/HaikuPairingPrompt.*`,
  `__HAIKU__`-only): a modal `BAlert` (`B_WARNING_ALERT`, buttons "Nega"/"Consenti", Escape
  denies) shown on the calling peer thread, returning the user's choice. End-user text is
  Italian; the peer key is never shown and the friendly name is sanitized (control bytes
  stripped, length-capped) since it is attacker-controlled. With no `BApplication` running it
  fails safe by denying. The `BAlert` usage is verified against the Haiku headers and the API
  reference (the synchronous `Go()` returns the 0-based button index, or -1 on quit, and
  deletes the alert before returning). Build + fail-safe checked on Haiku
  (`tests/ui/test_haikuprompt`, links against libbe); the live allow/deny dialog needs a
  `BApplication` host and a person, so it is not automated.

- The resident daemon. `ServerNode` (`src/traghetto/server/ServerNode.*`) is the portable
  wiring: it loads-or-generates the identity, loads the trust store, builds the server
  `TlsContext`, listens (default port 7735, overridable), and runs one `Daemon` over a
  `TlsChannelFactory` and a `GatedHandlerFactory` wrapping the `FileServerFactory`, with a
  `Pairing` over an injected prompt. Because the prompt is injected, the whole stack is tested
  end to end over loopback with a fake prompt (`tests/server/test_servernode`): a real TLS
  client pairs through the gate, browses the shared folder, an unknown peer is refused when
  the prompt denies, and the pairing is persisted. `DaemonApp`
  (`src/traghetto/server/DaemonApp.*`, `__HAIKU__`-only) is the thin `BApplication` shell that
  owns `be_app`: on `ReadyToRun` it resolves the paths (`trust/Paths`: identity, trust store,
  and the shared root `<home>/Desktop/Condivisa`), derives the node name from the hostname, and
  starts a `ServerNode` with a `HaikuPairingPrompt`; `daemon_main.cpp` is its entry point. The
  daemon binary builds and links against libbe on Haiku (`tests/server` `daemon-build`); it is
  not run there, since that would start a real listening daemon.

So the full native read path is complete in software (Fondamenta interface -> CNP -> TLS ->
daemon -> real filesystem, and back), and native mode now has a resident host: the
`BApplication` daemon owns `be_app`, serves the shared folder, and shows the pairing dialog
through the trust gate. Discovery (Bricola) now advertises this daemon and browses for peers
(`src/bricola/`, runtime-verified same-host; see `docs/VERIFIED.md` section 11). The write and
attribute messages are coded (schemas above) and their server-side handlers are implemented
(`FileServer`: WRITE, MKDIR/UNLINK/RENAME/TRUNCATE, READ_ATTRS/WRITE_ATTRS, gated by a
per-peer `writable` flag defaulting to read-only, with the shared-root boundary enforced on
every path, both endpoints of RENAME, and O_NOFOLLOW on created leaves; end-to-end tested in
`tests/server/test_filewrite`), and the client drives them (`CnpBackend` implements the
PeerBackend write methods, loopback-tested end to end in `tests/fondamenta/test_cnpbackend`
against a writable daemon: OpenWrite/Write/Close round-trips content, the namespace mutations
verify through Stat, and a read-only peer refuses writes). Still to write: the QUERY messages,
and the Haiku-side Fondamenta front end that drives a PeerBackend from FUSE / fs_interface.

## Framing (provisional; implemented, frozen in M2)

The frame envelope is implemented in `src/traghetto/wire/` (`Frame.h`, `FrameCodec.*`)
with golden and hostile-input tests in `tests/wire/`. Values below are provisional until
the M2 freeze; the code, this section, and the tests are kept in lockstep (rule 6).

```
offset  size  field         value / notes
0       2     magic         'C','N'  (0x43 0x4E)
2       1     version       0x01 (kProtocolVersion)
3       1     type          MessageType (see table)
4       4     request_id    u32, big-endian
8       4     payload_len   u32, big-endian
12      N     payload       payload_len bytes, opaque CBOR
```

- **Byte order is big-endian** (network order), consistent with carrying attribute type
  codes as their 4 ASCII chars in header order (see Attribute fidelity).
- Header is a fixed **12 bytes**; payload is opaque to the framing layer (CBOR is decoded
  a layer up).
- `request_id` allows pipelining and out-of-order replies; echoed by the reply.
- **Max payload = 16 MiB** (`kMaxPayloadLength`). A declared `payload_len` above this is a
  protocol error, rejected without allocating (untrusted-peer guard, rule 7).
- **Decoder is strict on magic and version** (a mismatch latches a terminal error and the
  connection is dropped) but treats a merely truncated frame as "need more data", not an
  error. The `type` byte is carried verbatim so new message types need no framing change.
- De-chunking uses an incremental `Feed(chunk)` / `Next(frame)` buffer, the discipline
  harvested from `Sotoportego/src/backend/OpenVPNManagement.cpp`, rewritten for a fixed
  header plus length prefix instead of newline splitting.

## Message types (initial set, frozen in M2)

Numeric codes are the `MessageType` enum in `src/traghetto/wire/Frame.h`, grouped by
nibble.

| Code | Type | Purpose |
| --- | --- | --- |
| 0x01 / 0x02 | `HELLO` / `WELCOME` | capability handshake, version negotiation, node identity |
| 0x10 | `LIST` | directory listing; each entry carries name, stat, full attribute set |
| 0x11 | `STAT` | single entry, full attributes |
| 0x12-0x15 | `OPEN` / `READ` / `WRITE` / `CLOSE` | file IO |
| 0x20-0x23 | `MKDIR` / `UNLINK` / `RENAME` / `TRUNCATE` | namespace mutation |
| 0x30-0x32 | `LIST_ATTRS` / `READ_ATTRS` / `WRITE_ATTRS` | BFS extended attributes, 1:1 fidelity |
| 0x40-0x43 | `QUERY_OPEN` / `QUERY_RESULT` / `QUERY_UPDATE` / `QUERY_CLOSE` | distributed live queries |
| 0xE0 | `ERROR` | error reply carrying a code + human-safe message |

## Payload encoding (CBOR subset)

Payloads are CBOR (RFC 8949), encoded with a minimal hand-rolled codec in
`src/traghetto/wire/Cbor.{h,cpp}` (tested against the RFC Appendix A vectors). The subset
is deliberate:

- Supported: unsigned int (major 0), negative int (1), byte string (2), text string (3),
  array (4), map (5), and `false`/`true`/`null` from major 7. Definite lengths only.
- Encoding is canonical (shortest form), so output is deterministic for golden tests and
  future signing. Map key ordering is the message layer's responsibility.
- Rejected on decode: floats, tags (major 6), indefinite-length items, and reserved
  additional-info values. Untrusted-input safe: no over-read, no oversized allocation, and
  `Skip()` (used to ignore unknown map keys) is iterative so hostile nesting cannot
  overflow the stack.

Per-message CBOR field schemas are defined alongside the message semantics (M2), each a
map keyed by short field names.

## HELLO / WELCOME schema (implemented)

The capability handshake. HELLO (`type` 0x01) is sent by the initiator; WELCOME
(`type` 0x02) is the responder's reply and echoes HELLO's `request_id`. Both carry the
same body, a NodeIdentity map. In HELLO `v` is the highest protocol version the initiator
speaks; in WELCOME `v` is the negotiated version. Implemented in
`src/traghetto/wire/Handshake.{h,cpp}`, tested in `tests/wire/test_handshake.cpp`.

Body is a CBOR map with these fields, emitted in canonical (length-first) key order
`v`, `fp`, `caps`, `node`:

| Key | CBOR type | Meaning |
| --- | --- | --- |
| `v` | uint | protocol version (nonzero) |
| `fp` | byte string, 32 bytes | identity fingerprint: SHA-256 over the SPKI (`i2d_PUBKEY`) |
| `caps` | array of text | capability tokens (open vocabulary; e.g. `bfs` = native BFS attributes + live query) |
| `node` | text | friendly name, an attacker-controlled label (section 9), not identity |

`fp` is the application-level identity and must equal the SPKI of the TLS-verified peer
certificate; a mismatch is rejected at a higher layer. Trust pins `fp`, never `node`.

Decode is order-independent and strict (untrusted input, rule 7): `fp` must be exactly 32
bytes, `node` at most 128 bytes, `caps` at most 32 tokens of at most 32 bytes each; a
missing or duplicate mandatory field, a zero version, or any trailing bytes after the map
are rejected; unknown keys are skipped for forward compatibility.

## Attribute fidelity (AttrSet, implemented)

An AttrSet is a CBOR array of attribute maps, each with canonical key order `n`, `t`, `v`.
Implemented in `src/traghetto/wire/Attributes.{h,cpp}`, tested in
`tests/wire/test_listing.cpp`. Reused by STAT/LIST entries, READ_ATTRS/WRITE_ATTRS, and
query results.

| Key | CBOR type | Meaning |
| --- | --- | --- |
| `n` | text (1..255) | attribute name (`B_ATTR_NAME_LENGTH` = 255) |
| `t` | uint | Haiku `type_code`, the 32-bit value of the FourCC |
| `v` | byte string | raw attribute value (may be empty) |

- `type` travels as a CBOR unsigned integer (the numeric `type_code`), not as raw header
  bytes. Verified codes (`TypeConstants.h:15-70`), as 32-bit values:
  `B_STRING_TYPE='CSTR'`=0x43535452, `B_INT32_TYPE='LONG'`=0x4C4F4E47,
  `B_INT64_TYPE='LLNG'`, `B_FLOAT_TYPE='FLOT'`, `B_DOUBLE_TYPE='DBLE'`,
  `B_BOOL_TYPE='BOOL'`, `B_TIME_TYPE='TIME'`, `B_MIME_STRING_TYPE='MIMS'`=0x4D494D53,
  `B_RAW_TYPE='RAWT'`=0x52415754, `B_MESSAGE_TYPE='MSGG'`. Note `B_STRING_TYPE` is
  `'CSTR'`, not `'TEXT'`.
- There is no separate size field: the attribute size is the `v` byte-string length.

The receiver writes each attribute back verbatim via `BNode::WriteAttr(n, t, 0, v,
v.size())` (or `fs_write_attr`), so a file copied Campiello to Campiello keeps its MIME
type, icon, ratings, and app-specific attributes exactly, with the real type code
preserved. The producer reads attributes with `BNode::RewindAttrs()` + `GetNextAttrName()`
+ `GetAttrInfo()` + `ReadAttr()` (`Node.h:58-70`), and carries them only for volumes where
`BVolume::KnowsAttr()` is true (`Volume.h:52`).

Decode is strict: an empty or over-255-byte name, a duplicate key within an attribute, a
missing field, or an array over `kMaxAttrs` (4096) is rejected; unknown keys are skipped.

## STAT / LIST schema (implemented)

STAT (`type` 0x11) and LIST (`type` 0x10) requests share the body `{ path }` (`path` text,
1..1024 bytes = `B_PATH_NAME_LENGTH`). A reply reuses the request's frame type and
`request_id`; a failure comes back as an `ERROR` frame instead. Implemented in
`src/traghetto/wire/Listing.{h,cpp}`.

**Stat** is a CBOR map, canonical key order `m`, `ct`, `mt`, `sz`, `ino`, from the verified
`struct stat` (`headers/posix/sys/stat.h`):

| Key | CBOR type | struct stat field |
| --- | --- | --- |
| `m` | uint | `st_mode` (type bits + permissions) |
| `ct` | int | creation time `st_crtim.tv_sec` (the BFS-meaningful one) |
| `mt` | int | modification time `st_mtim.tv_sec` |
| `sz` | uint | `st_size` |
| `ino` | uint | `st_ino` |

uid/gid are omitted (per-machine sharing, no user matrix, section 2); sub-second times and
atime are deferred.

**Entry** is `{ name, stat, attrs }` (canonical order `name`, `stat`, `attrs`): the leaf
name (text, 1..255), a Stat, and the full AttrSet.

- **STAT reply** body is a single Entry.
- **LIST reply** body is `{ entries: [Entry...] }` (up to `kMaxEntriesPerReply` = 65536;
  large-directory streaming/pagination is deferred, section 14).

Decode is strict and order-independent (unknown keys skipped): missing/duplicate mandatory
fields, an empty or oversized name/path, or trailing bytes after the payload are rejected.

**Architecture note (verified M0):** CNP carries the type code explicitly, so native
fidelity is full and does NOT depend on the userlandfs FUSE xattr channel - which is
read-only and flattens all non-MIME types to `B_RAW_TYPE`
(see `docs/VERIFIED.md` Risks item 1). Native mode must therefore reach real typed
attribute IO (Haiku-native userlandfs interface or a patched bridge), not the stock FUSE
shim, to honor attribute writes on the receiving volume.

## ERROR schema (implemented)

Any request that fails is answered with an ERROR frame (`type` 0xE0) whose `request_id`
matches the failed request. Implemented in `src/traghetto/wire/Error.{h,cpp}`. Body is a
CBOR map, canonical order `msg`, `code` (`msg` omitted when empty, giving a one-entry map):

| Key | CBOR type | Meaning |
| --- | --- | --- |
| `code` | uint | stable wire error code (see below); mandatory |
| `msg` | text (<= 256) | developer detail for logs; optional, not shown verbatim |

Codes: 1 internal, 2 not-found, 3 access-denied, 4 not-a-directory, 5 is-a-directory,
6 invalid-request, 7 unsupported, 8 IO-error, 9 no-space, 10 bad-handle, 11 too-many-open.
These are our own stable values, not raw errno; unknown codes are tolerated by decoders
(mapped to a generic failure) for forward compatibility. The user sees a short human string
the client renders from the code (localized to Italian); `msg` goes to the developer log
(error-surface rule, PROPOSAL.md section 13).

## OPEN / READ / WRITE / CLOSE schema (implemented)

The file IO path. Request and reply share the message type and `request_id`; a failure
returns an ERROR frame instead. Implemented in `src/traghetto/wire/FileOps.{h,cpp}`. The
handle is an opaque uint64 the responder assigns at OPEN and the requester passes back.

| Message | Request body | Reply body |
| --- | --- | --- |
| OPEN (0x12) | `{ mode: uint, path: text 1..1024 }` | `{ size: uint, handle: uint }` |
| READ (0x13) | `{ handle: uint, length: uint, offset: uint }` | `{ data: bytes }` |
| WRITE (0x14) | `{ data: bytes, handle: uint, offset: uint }` | `{ written: uint }` |
| CLOSE (0x15) | `{ handle: uint }` | Ok (empty map `{}`) |

- `mode` is a bitmask: `kOpenRead` = 1, `kOpenWrite` = 2. The read path uses `kOpenRead`;
  WRITE needs a handle opened with `kOpenWrite` (which creates/truncates on the responder,
  PROPOSAL.md section 8 phase 2).
- A READ requests at most `kMaxReadLength` = 1 MiB, and a reply carries at most that many
  bytes; a reply shorter than the requested `length` signals EOF. A WRITE carries at most
  `kMaxWriteLength` = 1 MiB; the reply's `written` is how many bytes were stored (normally
  the full `data` length).
- Ok is a reusable empty-map ack; its decoder tolerates future fields.
- Keys are emitted in canonical (length-first) order: OPEN request `mode, path`; OPEN reply
  `size, handle`; READ request `handle, length, offset`; WRITE request `data, handle,
  offset`. Decode is order-independent and strict (missing/duplicate fields, an
  empty/oversized path, an over-cap length, or trailing bytes are rejected; unknown keys
  skipped).

## Namespace mutation schema (implemented)

The write path's directory operations, in `src/traghetto/wire/Namespace.{h,cpp}`. Each
request is answered with an Ok ack on success or an ERROR frame on failure. The responder
enforces the shared-root boundary (both paths, for RENAME) and the per-peer read-only
default before acting; these schemas carry no permission bit, the policy lives server-side.

| Message | Request body |
| --- | --- |
| MKDIR (0x20) | `{ mode: uint, path: text 1..1024 }` |
| UNLINK (0x21) | `{ path: text 1..1024 }` (removes a file or an empty directory) |
| RENAME (0x22) | `{ to: text, from: text }` (both 1..1024, both inside the shared root) |
| TRUNCATE (0x23) | `{ path: text 1..1024, size: uint }` |

- UNLINK reuses the `{ path }` body shared with STAT/LIST. RENAME keys are emitted `to,
  from` (canonical length-first). Decode is strict and order-independent as elsewhere.

## Attribute messages (READ_ATTRS / WRITE_ATTRS, implemented)

Dedicated BFS-attribute transfer when a full STAT/LIST Entry is not needed, in
`src/traghetto/wire/AttrOps.{h,cpp}`. Both carry an AttrSet (see Attribute fidelity), so
type codes round-trip exactly. WRITE_ATTRS is the headline M3 capability.

| Message | Request body | Reply body |
| --- | --- | --- |
| READ_ATTRS (0x31) | `{ path: text 1..1024 }` | `{ attrs: AttrSet }` |
| WRITE_ATTRS (0x32) | `{ path: text 1..1024, attrs: AttrSet }` | Ok |

- READ_ATTRS reuses the `{ path }` body. WRITE_ATTRS replaces the named attributes on the
  target; the responder validates each attribute name and enforces the boundary and
  read-only default before writing. `LIST_ATTRS` (0x30) is reserved but not implemented:
  STAT/LIST already carry the full AttrSet, so name-only enumeration is deferred.

## Distributed live query (headline feature)

- Client sends `QUERY_OPEN` with a Haiku textual query predicate string. Transmit the
  textual predicate (`BQuery::GetPredicate`, `Query.h:69`) and rebuild it per-peer with
  `SetPredicate` + `SetVolume` (`:63-64`); avoid re-serializing the RPN op stack.
- Each peer, on a volume where `BVolume::KnowsQuery()` is true (`Volume.h:53`), builds a
  local `BQuery`, calls `SetTarget(BMessenger)` (`Query.h:65`) to make it live, then
  `Fetch()`. Initial matches stream back as `QUERY_RESULT`.
- Live updates arrive at the target as BMessages `what=B_QUERY_UPDATE='QUPD'`
  (`AppDefs.h:120`) with an `opcode` of `B_ENTRY_CREATED=1` / `B_ENTRY_REMOVED=2`
  (`NodeMonitor.h:36-37`). The peer forwards these as `QUERY_UPDATE`.
- **The kernel live event carries only `{ opcode, device, directory, node, name }`** (the
  field keys are literal strings, not named constants - `NodeMonitor.h:31-34`) - a node
  ref plus parent dir and leaf name, NO path and NO attribute values. The peer must
  resolve the `entry_ref` and read stat + attributes locally before streaming the entry.
Surprise recorded in M0: `BQuery::SetTarget` has ONLY a `BMessenger` overload - there is
no `SetTarget(BHandler*)`.

### Implemented wire schema (`wire/Query.h`, unit-tested in `tests/wire/test_query.cpp`)

Every frame carries a client-assigned `queryId` (a `uint`) so the streamed results and the
asynchronous updates correlate to the open query across many frames (a single `requestId`
cannot, because updates are unsolicited). CBOR keys are in canonical order.

| Message | Payload |
|---------|---------|
| `QUERY_OPEN` (0x40) request  | `{ query: text 1..4096, queryId: uint }` |
| `QUERY_RESULT` (0x41) reply  | `{ done: bool, entries: [Entry], queryId: uint }` |
| `QUERY_UPDATE` (0x42) async  | `{ added: bool, entry: Entry, queryId: uint }` |
| `QUERY_CLOSE` (0x43) request | `{ queryId: uint }` -> Ok (empty map) |

- `QUERY_RESULT` streams the initial match set in one or more batches; `done=true` marks the
  last batch. `entries` reuses the STAT/LIST `Entry` (name + stat + AttrSet), so the peer
  resolves each live `entry_ref` and reads stat + attributes locally before streaming (the
  kernel event carries no path or attribute values, per above).
- `QUERY_UPDATE` reuses the same `Entry`; `added=true` maps to `B_ENTRY_CREATED`,
  `added=false` to `B_ENTRY_REMOVED`.

Status (2026-07-21): the initial-result path is implemented and tested end to end.
- Wire codec + frame builders: `wire/Query.{h,cpp}`, `tests/wire/test_query.cpp` (round-trip +
  hostile inputs).
- Server: `FileServer::HandleQueryOpen` runs a real `BQuery` on the shared root's volume, filters
  every match to inside the shared root (the security invariant that stops a query leaking files
  outside it), caps the count by a quota, and returns one `QUERY_RESULT`. Verified live in
  `tests/fondamenta/test_cnpbackend.cpp` (a marker file found by a name query through the real
  server + BQuery).
- Client: `CnpBackend::Query` (open, collect, best-effort close).
- Aggregation + FUSE: `QueryAggregator` fans the predicate to every peer, merges + dedups by
  `(peer, path)` under a quota; `PathRouter` exposes it as a virtual `/.query/<predicate>` folder
  (`tests/discovery/test_pathrouter.cpp`).

The live loop is now implemented and verified end to end. ServeConnection runs a sole reader thread
(SSL_read) and a dedicated FrameWriter thread (SSL_write) on each connection, the model a TLS 1.3
connection supports. The server opens a live BQuery (B_LIVE_QUERY + SetTarget(BMessenger)), and each
B_QUERY_UPDATE is resolved (within the shared root), turned into a QUERY_UPDATE frame, and pushed
through the writer. The client (`LiveQueryClient`) keeps the query open on a dedicated channel and
delivers each update to a callback. `tests/fondamenta/test_cnpbackend.cpp` drives the whole path over
a real connection: open a live query, create a matching file on the server, receive the "added"
update, remove it, receive the "removed" update.

Remaining follow-up: batched `QUERY_RESULT` (`done=false`, multiple frames for a very large initial
set) is a minor optimization, not a correctness gap.

## Security invariants

- A malicious or buggy peer must never escape the shared root or smuggle attributes
  that corrupt the local index.
- Path canonicalization and attribute validation are required on both ends.
- Treat every peer as untrusted input (working agreement rule 7).
