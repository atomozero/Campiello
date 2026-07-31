# campiello_afp — AFP add-on

A Campiello device add-on (see `docs/DEVICE_ADDONS.md`) for an AFP (Apple Filing Protocol) server. It
shows the server's public info via an unauthenticated probe. Tenth component of the suite in
`docs/ADDONS_SUITE.md`.

## What it is

Older Apple file servers (and netatalk on NAS boxes) speak AFP, advertised over mDNS as
`_afpovertcp._tcp`. AFP is **legacy**: modern macOS shares over SMB, and Campiello already has an SMB
add-on. This add-on therefore does not try to be a file client; it just surfaces what the server
advertises.

## How it works (protocol)

AFP rides on **DSI** (Data Stream Interface) over TCP port 548. The DSI header is 16 bytes: flags(1),
command(1), requestID(2), writeOffset/errorCode(4), totalDataLength(4), reserved(4).

- `DSIGetStatus` (command 3) needs no session and no login. The client sends a 16-byte header with
  command 3 and an empty payload; the server replies with the **GetSrvrInfo** block.
- GetSrvrInfo starts with 2-byte offsets (relative to the block) to the machine type, the AFP-version
  list, and the UAM (user-authentication method) list, then flags, then the server name as a Pascal
  string at offset 10. Each list is a 1-byte count followed by that many Pascal strings.
- Everything else (mounting a volume, browsing) requires a DSI session (`DSIOpenSession`), the AFP
  command set, and UAM authentication (DHX2, etc.) - a large module, and a documented follow-up.

## Integration into Campiello

- `optional/afp/AfpProbe.{h,cpp}`: hand-rolled DSI GetStatus over plain TCP. `BuildGetStatusRequest`
  and `ParseGetSrvrInfo` (Pascal strings, count-prefixed lists) are dependency-free and unit-tested.
  No third-party library, so **MIT-clean**.
- `optional/afp/campiello_afp.cpp`: the info app. Probes the server on a worker thread and shows the
  name, machine type, AFP versions, and authentication methods, plus a note recommending SMB.
  `RefsReceived` reads `CAMPIELLO:host/name` from the WON device shortcut.
- `optional/afp/afp.handler`: matches `_afpovertcp._tcp`. `packaging/afp` builds `campiello_afp-0.1.0-1`
  (needs only `libbe`/`libnetwork`).

## Licensing

No third-party code or library. DSI/AFP are implemented from the public Apple specification; the
netatalk project documents the same structures. Fits the MIT core rule (kept under `optional/` only
because it is device-specific).

## Reference material

- Apple "AFP 3.4" / DSI specification for the DSI header and the `DSIGetStatus` / GetSrvrInfo layout.
- The netatalk project (open-source AFP server) for the same GetSrvrInfo structure.

## Testing status

The GetSrvrInfo parser is unit-tested (a synthetic block decodes to the right server name, machine
type, AFP versions, and UAMs; the GetStatus request is the expected 16 bytes). **Not yet validated
against a live AFP server** (none in the dev environment). To validate: run `campiello_afp host=<ip>`
and check the shown info.

## Follow-ups

- A native AFP client (DSI sessions + the AFP command set + UAM authentication) to browse and
  download. This is a large module; SMB is the pragmatic path for file sharing and Campiello already
  covers it.
