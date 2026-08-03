# campiello_spotify - Spotify Connect add-on

A Campiello device add-on (see `docs/DEVICE_ADDONS.md`) for a Spotify Connect receiver. It shows the
speaker's public info; it does **not** control playback. Thirteenth component of the suite in
`docs/ADDONS_SUITE.md`.

## What it is

Spotify Connect speakers (Sonos, AVRs, `librespot` devices) advertise themselves over mDNS as
`_spotify-connect._tcp`. Users want to see what a discovered speaker is.

## How it works

- The receiver runs a small HTTP server (the "zeroconf" endpoint). Its **unauthenticated** `getInfo`
  action returns public device info as JSON: `remoteName`, `brandDisplayName`, `modelDisplayName`,
  `deviceType`, `version`, `activeUser`, plus the device's DH `publicKey`. The endpoint path is the
  `CPath` value from the mDNS TXT (often `/`), the port is the SRV port:
  `GET http://<host>:<port><CPath>?action=getInfo`.
- **Controlling playback** (transfer to this device, play/pause/next) needs a Spotify **Premium**
  account: either the Web API over OAuth, or the closed Connect protocol (the zeroconf `addUser`
  action takes an encrypted `blob`/`clientKey` derived from the account). That is a **documented
  follow-up** requiring account authentication - it is NOT implemented here (no fake control).

## Integration into Campiello

- `optional/spotify/SpotifyProbe.{h,cpp}`: a `getInfo` client over plain HTTP with a tiny JSON string
  extractor. No third-party library, so **MIT-clean**. `JsonString` is unit-tested.
- `optional/spotify/campiello_spotify.cpp`: the info app. `RefsReceived` reads `CAMPIELLO:host/name`,
  the SRV port from `CAMPIELLO:port`, and `CAMPIELLO:txt.CPath`, then probes `getInfo` on a worker
  thread and shows the speaker info with a note that control needs a Premium account. Links `libbe` +
  the network kit.
- **WON plumbing**: `src/vicinato/ShareFolder.cpp` now also writes the SRV port to the device shortcut
  as `CAMPIELLO:port` (a generic enhancement, alongside the `CAMPIELLO:txt.<key>` attributes), so any
  add-on can reach the device without re-querying mDNS.
- `optional/spotify/spotify.handler`: matches `_spotify-connect._tcp`. `packaging/spotify` builds
  `campiello_spotify-0.1.0-1`.

## Licensing

No third-party code or library. The getInfo endpoint is documented in Spotify's ZeroConf API for
commercial hardware; `librespot` is the open reference. Fits the MIT core rule.

## Reference material

- Spotify "commercial hardware" ZeroConf API (the `getInfo` / `addUser` actions and the TXT keys).
- `librespot-org/librespot` (and its Go/Java ports) for the zeroconf endpoint and field names.

## Testing status

The getInfo JSON parser is unit-tested (a synthetic response decodes to the right name/brand/model/
type/version/user). **Not yet validated against a live receiver** (none in the dev environment). To
validate: run `campiello_spotify host=<ip> port=<n>` and check the shown info.

## Follow-ups

- Playback control: OAuth Web API device control, or the Connect `addUser` handshake (adds account
  auth + crypto), kept in the optional set.
- Read `Stack`/`VERSION` from the TXT and show more device detail.
