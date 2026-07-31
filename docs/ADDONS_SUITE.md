# Campiello device add-on suite — master plan

Campiello's WON neighborhood recognizes many DNS-SD network services (see
`src/bricola/mdns/RadarLabels.cpp` and `src/vicinato/NetworkDirectory.cpp`). This document is the
master plan for turning each recognizable service into an optional Campiello **device add-on** (the
framework in `docs/DEVICE_ADDONS.md`: a separate app matched by a `*.handler` manifest and launched
on a double-click of the device in `~/WON`).

It is worked through autonomously, one component per iteration, ~5 minutes apart. Each component is a
"super task" split into the same sub-tasks:

1. **Research — what it is**: the service, the device class, what users do with it.
2. **Research — how it works**: the on-the-wire protocol, ports, auth/pairing, discovery TXT keys.
3. **Research — integration**: how it maps onto a Campiello add-on (handler manifest + app + actions),
   and the licensing of any library needed (must fit the core rule or stay optional/dynamically-loaded).
4. **Reference code**: find MIT/BSD/Apache example code on GitHub; note what to harvest, what to avoid.
5. **Module**: scaffold `optional/<name>/` + `packaging/<name>/`, following the `firetv` pattern
   (handler manifest, app_signature, RefsReceived reading `CAMPIELLO:host/name/type`, Locale Kit).
6. **Docs**: a developer note (`docs/addons/<name>.md`) and user-facing strings/help.

Rules carried from `CONTRIBUTING.md`: verify every API against Haiku headers; core deps MIT/BSD/Apache/ISC/
zlib/public-domain (LGPL/GPL only under `optional/`, dynamically loaded, off by default); English for
code and dev docs, Italian for end-user strings; no em dashes; small reviewable commits; never
mount/unmount userlandfs on this machine.

## Legend

- **Status**: `todo` / `research` / `scaffold` / `functional` / `done`.
- **Feasibility**: how much can be implemented here vs needs the physical device / heavy crypto.

## Backlog (loop order)

Already shipped: Campiello (native), SMB (`campiello_smb`), SFTP/SSH (`campiello_sftp`/`_mount`),
Web (bookmark), Amazon Fire TV (`campiello_firetv`, `_amzn-wplay._tcp`).

| # | Add-on | mDNS type(s) | Kind | Feasibility | Status |
|---|--------|--------------|------|-------------|--------|
| 1 | **campiello_hue** | `_hue._tcp` | Home | high — local HTTP/JSON REST, no heavy crypto | functional (needs live bridge) |
| 2 | **campiello_ipp** | `_ipp._tcp` (`_ipps._tcp` TLS follow-up) | Printer | high — IPP is documented; Haiku has printing | functional (needs live printer) |
| 3 | **campiello_escl** | `_uscan._tcp` (`_uscans._tcp` TLS follow-up) | Printer | high — eSCL is HTTP/XML | functional (needs live scanner) |
| 4 | **campiello_cast** | `_googlecast._tcp` | Media | medium — DIAL launch done; CASTv2 protobuf follow-up | functional (DIAL; needs live device) |
| 5 | **campiello_daap** | `_daap._tcp` | Media | medium — HTTP-based iTunes library | functional (needs live server) |
| 6 | **campiello_vnc** | `_rfb._tcp` | System | medium — launches a viewer; native RFB client follow-up | functional (hand-off) |
| 7 | **campiello_airplay** | `_airplay._tcp` `_raop._tcp` | Media | hard (streaming needs AirPlay 2 crypto); info+features done | functional (info; streaming follow-up) |
| 8 | **campiello_webdav** | `_webdav._tcp` (`_webdavs._tcp` TLS follow-up) | File | medium — HTTP file backend | functional (needs live server) |
| 9 | **campiello_afp** | `_afpovertcp._tcp` | File | low — legacy; info probe only, prefer SMB | functional (info probe; needs live server) |
| 10 | **campiello_nfs** | `_nfs._tcp` | File | medium — showmount via RPC done; native client follow-up | functional (export list; needs live server) |
| 11 | **campiello_ftp** | `_ftp._tcp` | File | high — FTP is simple | functional (needs live server) |
| 12 | **campiello_homekit** | `_hap._tcp` | Home | hard (control needs crypto); info from TXT done | functional (info; control follow-up) |
| 13 | **campiello_matter** | `_matter._tcp` `_matterc._udp` | Home | hard (control needs SDK/crypto); info from TXT done | functional (info; control follow-up) |
| 14 | **campiello_spotify** | `_spotify-connect._tcp` | Media | hard (control needs account); getInfo done | functional (info; control follow-up) |
| 15 | **campiello_alexa** | `_amzn-alexa._tcp` | Media | low — no open local control API (info only) | functional (info only) |
| 16 | **campiello_lutron** | `_sleap._tcp` | Home | medium (control needs LEAP TLS pairing); info done | functional (info only) |

Feasibility "hard/low" items get a research note + a scaffold marked "needs hardware / blocked on
open API", not a fake implementation. Working modules are built where a local, documented,
license-clean protocol exists (Hue, IPP, eSCL, FTP, WebDAV, DAAP, Cast-DIAL first).

## Progress log

Newest first. Each entry: component, what was researched/built, commit.

- **Iteration 16 (final) — campiello_airplay.** Honest scope: streaming/mirroring need the AirPlay 2
  handshake + FairPlay (Apple crypto), a follow-up NOT faked. Built the info path: `AirplayInfo`
  decodes the AirPlay mDNS TXT (model/deviceid/srcvers/features/pw) and turns the 64-bit `features`
  bitfield (one or two hex words) into readable capability labels (Video, Audio, Screen mirroring,
  AirPlay 2, ...); unit-tested. `campiello_airplay` reads the CAMPIELLO:txt.* attributes and shows the
  receiver info + capabilities with a note about streaming. The manifest matches _airplay._tcp and
  _raop._tcp. References: unofficial AirPlay docs, pyatv, OwnTone.

- **Iteration 15 — campiello_lutron.** Honest scope: Lutron Secure LEAP (_sleap._tcp, TLS :8081) needs
  a client-certificate pairing (button press -> issued cert) then LEAP JSON over mutual TLS, a heavy
  follow-up NOT faked. Built an informational add-on: `campiello_lutron` reads CAMPIELLO:host/name +
  CAMPIELLO:port (fallback 8081) + CAMPIELLO:txt.* and shows the bridge presence and LEAP endpoint with
  a note about the pairing. No network. The `lutron.handler` matches _sleap._tcp; packaging +
  docs/addons/lutron.md. Reference: gurumitts/pylutron-caseta.

- **Iteration 14 — campiello_alexa.** Honest scope: Alexa has NO open local control API (control is
  cloud via the Alexa Voice Service and Smart Home Skill API, account-bound), so a LAN controller is
  impossible - NOT faked. Built an informational add-on: `campiello_alexa` reads CAMPIELLO:host/name +
  the CAMPIELLO:txt.* attributes and shows the device presence with a clear note that control needs
  the Amazon app/cloud. No network. The `alexa.handler` matches _amzn-alexa._tcp; packaging +
  docs/addons/alexa.md.

- **Iteration 13 — campiello_spotify.** Honest scope: playback control needs a Spotify Premium account
  + OAuth/Connect auth (follow-up, NOT faked). Built the info path: `SpotifyProbe` calls the
  unauthenticated zeroconf getInfo endpoint (GET <CPath>?action=getInfo) and parses the JSON
  (remoteName/brand/model/deviceType/version/activeUser); JsonString unit-tested. Added a second
  generic WON-core enhancement: `ShareFolder` now writes the SRV port as CAMPIELLO:port on the device
  shortcut, so add-ons reach the device without re-querying mDNS. `campiello_spotify` reads host/port/
  CPath and shows the speaker info on a worker thread with a note to use the Spotify app. Bumps
  campiello 0.3.10-1. References: Spotify ZeroConf API, librespot.

- **Iteration 12 — campiello_matter.** Honest scope like HomeKit: commissioning/control need PASE/CASE
  + attestation certificates + Thread/Wi-Fi onboarding (the Matter SDK/crypto, follow-up, NOT faked).
  Built the info path: `MatterInfo` decodes the Matter mDNS TXT (D/VP/CM/DT/DN/SII/SAI/SAT/T) into
  vendor/product, device type (DT -> Italian name), commissioning state, discriminator, and session
  timing (unit-tested). `campiello_matter` reads the CAMPIELLO:txt.* attributes (plumbed since iter 11)
  in RefsReceived and shows the device info with a note to pair via a Matter app/hub. The manifest
  matches both _matter._tcp and _matterc._udp. References: the Matter Core Spec, connectedhomeip,
  python-matter-server.

- **Iteration 11 — campiello_homekit.** Honest scope: control needs SRP pairing + ChaCha20-Poly1305 +
  Ed25519 (heavy crypto, follow-up, NOT faked). Built the info path: `HapInfo` decodes the _hap._tcp
  mDNS TXT (md/id/ci/sf/c#/pv) into name, category (ci -> Italian name), pairing state, config, and
  protocol (unit-tested). Enhanced the WON core (`ShareFolder`) to write each discovered TXT pair as a
  CAMPIELLO:txt.<key> attribute on the device shortcut - a generic improvement for every add-on - so
  `campiello_homekit` reads them in RefsReceived and shows the accessory info with a note to pair via
  Apple Home. Verified the attribute plumbing end-to-end. Bumps campiello 0.3.9-1. References: the HAP
  spec, HomeKitADK, hap-python/HAP-NodeJS.

- **Iteration 10 — campiello_afp.** Implemented the unauthenticated DSI GetStatus probe (AFP over DSI,
  TCP :548): send a 16-byte DSIGetStatus header, parse the GetSrvrInfo block (server name, machine
  type, AFP versions, UAMs). Built `AfpProbe` (hand-rolled DSI, no third-party dependency, MIT-clean;
  BuildGetStatusRequest + ParseGetSrvrInfo with Pascal strings/count-lists), `campiello_afp` (shows
  the server info on a worker thread with a note recommending SMB), the `afp.handler` manifest,
  packaging, and `docs/addons/afp.md`. The parser is unit-tested; needs a live server to validate.
  AFP is legacy (prefer the SMB add-on); a native AFP client is documented as a heavy follow-up.

- **Iteration 9 — campiello_nfs.** Implemented "showmount -e" over ONC RPC (RFC 1057/1833/1813):
  portmapper GETPORT to find mountd, then MOUNTPROC3_EXPORT for the export list. Built `NfsProbe`
  (hand-rolled RPC/XDR over UDP, no third-party dependency, MIT-clean; BuildCall, ParseReplyHeader,
  ParseExportList), `campiello_nfs` (lists exports as host:/export on a worker thread, shows mount
  instructions + a copy button, mounts NOTHING per the KDL rule), the `nfs.handler` manifest,
  packaging, and `docs/addons/nfs.md`. The RPC codec is unit-tested (export reply decode + GETPORT
  call size); needs a live server to validate. Follow-up: a native NFSv3/NFSv4 client.

- **Iteration 8 — campiello_webdav.** Implemented WebDAV (RFC 4918): PROPFIND Depth 1 -> 207
  multistatus, GET download, optional Basic auth. Built `WebDavClient` (pure HTTP/XML, no third-party
  dependency, MIT-clean; a namespace-agnostic multistatus parser with Blocks/Tag/ParseMultistatus,
  UrlDecode, and a small Base64), `campiello_webdav` (folder browser with navigation and file download
  via BFilePanel, each op on its own worker connection), the `webdav.handler` manifest, packaging, and
  `docs/addons/webdav.md`. The parser is unit-tested (folders/files, sizes, percent-decoded names,
  self-entry skipped); needs a live server to validate. Follow-up: HTTPS, PUT/MKCOL/DELETE.

- **Iteration 7 — campiello_ftp.** Implemented FTP (RFC 959) from scratch: control connection + login
  (anonymous/credentials), TYPE I, PASV data connections, LIST and RETR. Built `FtpClient` (pure
  sockets, no third-party dependency, MIT-clean; multi-line reply reader, PASV 227 parser, and a
  ParseListing for the Unix ls -l format), `campiello_ftp` (folder browser with navigation and
  file download via a BFilePanel, each op on its own worker connection), the `ftp.handler` manifest,
  packaging, and `docs/addons/ftp.md`. ParseListing is unit-tested (folders/files, sizes, names with
  spaces, symlinks); needs a live server to validate end-to-end. Follow-up: FTPS, upload, MLSD.

- **Iteration 6 — campiello_vnc.** Researched RFB/VNC (RFC 6143) and confirmed a full client is heavy
  (handshakes + Raw/CopyRect/Tight/ZRLE encodings + input); also found Haiku's RemoteDesktop is NOT
  RFB and stock Haiku has no vnc:// handler. Built `campiello_vnc` as a hand-off launcher: reads
  CAMPIELLO:host (port default 5900), builds vnc://host:port, and opens it via be_roster on the
  application/x-vnd.Be-URL.vnc scheme; falls back to showing host:port + an install hint + a copy
  button. The `vnc.handler` manifest matches _rfb._tcp; packaging + docs/addons/vnc.md. Compiles
  (libbe only). Native RFB client documented as a follow-up (NOT faked).

- **Iteration 5 — campiello_daap.** Researched DAAP/DMAP (HTTP :3689, /login -> mlid session, /databases
  -> miid, /databases/N/items with the minm/asar/asal content codes; DMAP is a binary TLV tree);
  references github.com/bjoernricks/daap-protocol and github.com/mattstevens/dmap-parser. Built
  `DaapClient` (hand-rolled DAAP + DMAP parser, no third-party dependency, MIT-clean; recursive
  FindLeaf, ParseTracks, container heuristic), `campiello_daap` (login + list the first database's
  tracks in a BListView, network I/O on a worker thread), the `daap.handler` manifest, packaging, and
  `docs/addons/daap.md`. The DMAP parser is unit-tested (session-id + two tracks decode correctly);
  needs a live server to validate. Follow-up: stream/play a track, playlists, password auth.

- **Iteration 4 — campiello_cast.** Researched Google Cast: DIAL (plain HTTP :8008, GET/POST/DELETE
  /apps/<AppName>, device-desc.xml) vs the heavier CASTv2 protobuf channel (TLS :8009); references the
  DIAL spec and github.com/geraldnilles/Chromecast-Server. Built `DialClient` (hand-rolled DIAL, no
  third-party dependency, MIT-clean; FriendlyName/AppState/Launch/Stop + a namespace-agnostic XmlTag),
  `campiello_cast` (device name + running app, launch YouTube/Netflix, stop, network I/O on worker
  threads), the `cast.handler` manifest, packaging, and `docs/addons/cast.md`. XmlTag is unit-tested;
  needs a live device to validate. CASTv2 media casting documented as a follow-up (NOT faked).

- **Iteration 3 — campiello_escl.** Researched eSCL/AirScan (HTTP/XML: GET ScannerCapabilities, POST
  ScanJobs -> 201 + Location, GET NextDocument with 200/202 polling); reference
  github.com/alexpevzner/sane-airscan. Built `EsclClient` (hand-rolled HTTP/XML, no third-party
  dependency, MIT-clean; namespace-agnostic tag readers that skip closing tags, ScanSettings builder),
  `campiello_escl` (capabilities summary + color/resolution/format menus + scan-to-file via a
  BFilePanel save panel, network I/O on worker threads), the `escl.handler` manifest, packaging, and
  `docs/addons/escl.md`. The XML parse + ScanSettings build are unit-tested; needs a physical scanner
  to validate live. Follow-up: read port/rs from the discovery TXT, _uscans._tcp (TLS), ADF multi-page.

- **Iteration 2 — campiello_ipp.** Researched IPP (HTTP POST of a binary message on :631,
  Get-Printer-Attributes 0x000B / Print-Job 0x0002, attribute tag encoding); references the PWG IPP
  guide and github.com/williamkapke/ipp. Built `IppClient` (hand-rolled encode/decode, no third-party
  dependency, MIT-clean), `campiello_ipp` (printer summary + attribute list + "Stampa un file" via
  BFilePanel, network I/O on worker threads), the `ipp.handler` manifest, packaging, and
  `docs/addons/ipp.md`. The encode/parse round-trip is unit-tested; needs a physical printer to
  validate live. Follow-up: `_ipps._tcp` (TLS) and reading rp/pdl from the discovery TXT.

- **Iteration 1 — campiello_hue.** Researched the Hue local REST API (HTTPS self-signed :443,
  link-button pairing via `POST /api` -> application key, v2 `clip/v2/resource/light` GET/PUT with the
  `hue-application-key` header); reference github.com/tigoe/hue-control. Built `HueBridge` (protocol
  client, TLS mirrors FireTVRemote), `campiello_hue` (pairing panel + light list with on/off + a
  brightness slider, network I/O on worker threads), the `hue.handler` manifest, packaging, and
  `docs/addons/hue.md`. Compiles; needs a physical bridge to validate live.

## Final summary (suite complete: 16/16)

All 16 recognizable non-file-core services now have an optional device add-on, plus the pre-existing
SMB and Fire TV add-ons. Each is a separate `optional/<name>/` module + `packaging/<name>/` hpkg,
matched by a `*.handler` manifest and launched from `~/WON` on a double-click (reading
`CAMPIELLO:host/name`, and now `CAMPIELLO:port` and `CAMPIELLO:txt.<key>`). Every module is MIT-clean:
those that need TLS (Fire TV, Hue) link OpenSSL (Apache-2.0) in their own optional package; none add a
GPL/LGPL or vendor SDK dependency to the core.

**Functional with a real, hand-rolled protocol** (compile + unit-tested codec where applicable; need a
live device to validate end-to-end): `campiello_hue` (Hue REST), `campiello_ipp` (IPP printing),
`campiello_escl` (eSCL scanning), `campiello_cast` (Google Cast via DIAL), `campiello_daap` (DAAP/DMAP
library), `campiello_ftp` (FTP), `campiello_webdav` (WebDAV), `campiello_nfs` (showmount via ONC RPC),
`campiello_afp` (DSI GetStatus), `campiello_spotify` (Connect getInfo). `campiello_vnc` hands off to an
installed viewer.

**Info-only, with the heavy control path documented as a follow-up (never faked)**: `campiello_homekit`
and `campiello_matter` (control needs SRP/PASE-CASE + ChaCha20-Poly1305 / the Matter SDK),
`campiello_airplay` (streaming needs the AirPlay 2 handshake + FairPlay), `campiello_lutron` (LEAP needs
a TLS client-certificate pairing), `campiello_alexa` (no open local control API at all).

**Two generic core enhancements** made along the way benefit every add-on: `src/vicinato/ShareFolder.cpp`
now writes each discovered mDNS TXT pair as a `CAMPIELLO:txt.<key>` attribute and the SRV port as
`CAMPIELLO:port` on the device shortcut, so an add-on gets the device's advertised metadata and port
without re-querying mDNS (campiello 0.3.10).

Each module ships a developer note in `docs/addons/<name>.md`. Follow-ups (TLS variants, full clients,
account/crypto control paths) are recorded there and in the table above; none require faking behavior.
