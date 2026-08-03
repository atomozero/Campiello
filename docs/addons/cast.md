# campiello_cast - Google Cast add-on

A Campiello device add-on (see `docs/DEVICE_ADDONS.md`) for a Google Cast / Chromecast device. It
speaks both Cast protocols: **CASTv2** (the real control channel) for device status, volume and media
casting, and **DIAL** for simple launch/stop of named apps. Fourth component of the suite in
`docs/ADDONS_SUITE.md`.

## What it is

Chromecast, Google TV, Nest displays and Google Home speakers advertise over mDNS as
`_googlecast._tcp`. Users want to read what the device is doing, adjust volume, cast a media URL, and
launch/stop apps from the computer.

## How it works (protocols)

- **CASTv2** - the real Cast control channel: a TLS connection to port **8009** carrying
  length-prefixed `CastMessage` **protobuf** frames (a `uint32` big-endian length + the message).
  `CastMessage` fields: protocol_version, source_id, destination_id, namespace, payload_type,
  payload_utf8. Control is JSON over named namespaces:
  - `...tp.connection` - `{"type":"CONNECT"}` opens a virtual connection (to `receiver-0`, then to a
    media session's transportId).
  - `...tp.heartbeat` - `PING`/`PONG` keepalive (the add-on answers PINGs while waiting).
  - `...receiver` - `GET_STATUS` (volume + running app), `LAUNCH` (start an app by id), `STOP`,
    `SET_VOLUME`.
  - `...media` - `LOAD` a media URL into the Default Media Receiver (`CC1AD845`), and `MEDIA_STATUS`.
  The device certificate is self-signed, so the TLS client does not verify it (a LAN device, exactly
  like the Fire TV add-on).
- **DIAL** (DIscovery And Launch) - a plain-HTTP REST protocol on port **8008**: `GET/POST/DELETE
  /apps/<AppName>` launches/stops/queries a named receiver app, and `GET /ssdp/device-desc.xml` gives
  the friendly name. Kept as a convenient fallback (and it also works where CASTv2 is blocked).

## What it does

- Reads the real state over CASTv2: current volume (shown as a percentage, with a mute flag) and the
  running application (display name + status text). Falls back to DIAL friendly-name + app-state when
  CASTv2 cannot connect.
- Casts a media URL: type a direct media URL, the add-on launches the Default Media Receiver and
  `LOAD`s it (the contentType is guessed from the extension, e.g. `video/mp4`, `audio/mpeg`,
  `application/x-mpegurl` for HLS).
- Adjusts volume (Vol +/- in 10% steps) via `SET_VOLUME`.
- Launches YouTube/Netflix and stops the running app (CASTv2 `STOP` when a session is known, else a
  DIAL stop).

## Screen mirroring (honest follow-up, not implemented)

Mirroring your Haiku desktop to the TV is **not** here and is not faked. Cast screen mirroring uses a
separate, proprietary Cast media-remoting pipeline (a WebRTC/RTP video stream with H.264/VP8,
negotiated through the cast channel) that has no open client implementation outside Chrome. Loading a
media URL (above) is the open, documented casting path; mirroring would require reverse-engineering
the closed remoting channel and a real-time video encoder, a large separate effort.

## Integration into Campiello

- `optional/cast/CastChannel.{h,cpp}`: the CASTv2 client - a hand-rolled `CastMessage` protobuf codec,
  tiny JSON readers, and a TLS channel over OpenSSL. `Connect`, `GetStatus`, `CastUrl`, `SetVolume`,
  `StopApp`. The codec and JSON readers are unit-tested off-device (`test_cast.cpp`).
- `optional/cast/DialClient.{h,cpp}`: hand-rolled DIAL over plain HTTP (`FriendlyName`, `AppState`,
  `Launch`, `Stop`; a namespace-agnostic `XmlTag`, unit-tested).
- `optional/cast/campiello_cast.cpp`: the panel app; all network I/O on worker threads. `RefsReceived`
  reads `CAMPIELLO:host/name` from the WON device shortcut.
- `optional/cast/cast.handler`: matches `_googlecast._tcp`. `packaging/cast` builds
  `campiello_cast-0.2.0-1` and links OpenSSL (`libssl`/`libcrypto`, Apache-2.0) for the TLS channel;
  the MIT core never depends on it.

## Testing status

The protobuf codec (round-trip, truncation) and the JSON readers (a realistic RECEIVER_STATUS) are
unit-tested, as is the DIAL `XmlTag`. Not yet validated against a physical Cast device (none on the
dev LAN - the test network had a Google Home zone but no `_googlecast` receiver). Some newer firmwares
restrict DIAL to whitelisted apps; CASTv2 media casting is the more general path.

## Follow-ups

- Live transport controls (play/pause/seek) and a media progress display via `MEDIA_STATUS`.
- Read the DIAL port / `Application-URL` from the device description instead of assuming `:8008`.
- The proprietary screen-mirroring channel (documented above) remains out of scope.
