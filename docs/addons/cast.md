# campiello_cast — Google Cast add-on

A Campiello device add-on (see `docs/DEVICE_ADDONS.md`) for a Google Cast / Chromecast device. It
uses DIAL (plain HTTP) to launch and stop receiver apps. Fourth component of the suite in
`docs/ADDONS_SUITE.md`.

## What it is

Chromecast and other Cast targets advertise themselves over mDNS as `_googlecast._tcp`. Users want to
throw an app (YouTube, Netflix) onto the TV and stop it from the computer.

## How it works (protocol)

Two layers exist; this add-on uses the simpler one.

- **DIAL** (DIscovery And Launch) is a plain-HTTP REST protocol on port **8008**:
  - `GET http://<ip>:8008/ssdp/device-desc.xml` returns the device description (`<friendlyName>`).
  - `GET http://<ip>:8008/apps/<AppName>` returns the app's state (`<state>running|stopped</state>`);
    `404` if the app is not installed.
  - `POST http://<ip>:8008/apps/<AppName>` launches the app (`201 Created`, `Location` = the run URI).
  - `DELETE http://<ip>:8008/apps/<AppName>/run` stops it.
- **CASTv2** (the protobuf channel over TLS on port **8009**) is what loads a specific media URL and
  drives transport controls (play/pause/seek/volume). It needs TLS plus protobuf framing and the
  receiver/media namespaces. This is a **documented follow-up**, not implemented here.

## Integration into Campiello

- `optional/cast/DialClient.{h,cpp}`: hand-rolled DIAL over plain HTTP. No third-party library, so
  **MIT-clean with no extra dependency**. `FriendlyName`, `AppState`, `Launch`, `Stop`; a
  namespace-prefix-agnostic `XmlTag` reader (unit-tested).
- `optional/cast/campiello_cast.cpp`: the panel app. On open it queries the device name and which of
  a small app list (YouTube, Netflix) is running, on a worker thread; buttons launch an app or stop
  the running one on another worker thread. `RefsReceived` reads `CAMPIELLO:host/name` from the WON
  device shortcut.
- `optional/cast/cast.handler`: matches `_googlecast._tcp`. `packaging/cast` builds
  `campiello_cast-0.1.0-1` (needs only `libbe`/`libnetwork`).

## Licensing

No third-party code or library. DIAL is implemented from its public specification. Fits the MIT core
rule (kept under `optional/` only because it is device-specific).

## Reference material

- DIAL specification (`dial-multiscreen.org`; summarized on Wikipedia "Discovery and Launch") and
  `github.com/geraldnilles/Chromecast-Server` — the port 8008 REST endpoints and the launch/stop
  flow.
- `github.com/balloob/pychromecast` and the `castv2` Node.js client document the CASTv2 protobuf
  channel for the media follow-up.

## Testing status

The `XmlTag` reader is unit-tested (a synthetic device description and app-state XML decode to the
right friendly name / app / state). **Not yet validated against a physical Cast device** (none in the
dev environment); note that some newer Chromecast firmwares restrict DIAL to whitelisted apps.

## Follow-ups

- CASTv2 media casting: load a media URL and expose play/pause/seek/volume (adds OpenSSL + protobuf;
  would move the module to the OpenSSL-linked optional set).
- Read the DIAL port and `Application-URL` from the device description instead of assuming `:8008`.
- Volume and mute via the receiver namespace.
