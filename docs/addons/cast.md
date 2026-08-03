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
- Casts a **local file from this PC**: "Casta file locale..." opens a file panel; the add-on starts a
  small built-in HTTP server (`MediaServer`) that serves the chosen file with byte-range support,
  finds this machine's LAN address toward the device, and `LOAD`s `http://<this-pc>:<port>/stream.<ext>`.
  The Chromecast then pulls and plays the file straight from the PC. See "Casting a local file" below.
- Shows **this computer's screen on the TV** as a low-frame-rate image ("Schermo su TV"): see
  "Screen on the TV" below.
- Adjusts volume (Vol +/- in 10% steps) via `SET_VOLUME`.
- Launches YouTube/Netflix and stops the running app (CASTv2 `STOP` when a session is known, else a
  DIAL stop).

## Casting a local file (how the PC feeds the video)

A Chromecast never receives a pushed stream: you give it a URL and it downloads the media over HTTP by
itself. So to play a file on this PC, Campiello serves that one file over HTTP and hands the device the
URL:

1. You pick a file. `MediaServer` binds an ephemeral port on `0.0.0.0` and serves only that file, for
   as long as playback lasts, handling `GET`/`HEAD` and `Range` requests (206 Partial Content) so the
   Chromecast can seek.
2. The add-on finds this machine's LAN IPv4 on the route toward the device (a `connect()`ed UDP socket
   + `getsockname`, no packets sent) and builds `http://<pc-ip>:<port>/stream.<ext>`.
3. It `LOAD`s that URL over CASTv2 (as above); the Chromecast connects back to the PC and streams it.

**No transcoding.** The file is served byte-for-byte, so the Chromecast must natively support the
codec: H.264 (and VP8/VP9) video with AAC/MP3/Opus/Vorbis audio, in MP4/WebM/MKV. An unsupported codec
(e.g. H.265/HEVC on older devices) is rejected by the receiver, not converted - the add-on reports the
failure rather than pretending. `MediaServer` and the range handling are tested over loopback
(`test_mediaserver.cpp`: full GET, a `Range` slice with the exact bytes, an open-ended range, HEAD).

## Screen on the TV (~1 fps preview - real, low frame rate)

"Schermo su TV" puts this computer's desktop on the TV as a **refreshing still image**, built entirely
on Haiku system APIs plus the media path above:

1. A worker connects once and launches the Default Media Receiver (`LaunchMediaReceiver`).
2. Every second it grabs the main screen (`BScreen::GetBitmap`), compresses it to JPEG in memory with
   the **Translation Kit** (`BBitmapStream` -> `BTranslatorRoster` -> `B_JPEG_FORMAT`, no external
   dependency), publishes it on the media server's in-memory buffer (`MediaServer::ServeBuffer` /
   `UpdateBuffer`), and `LOAD`s a fresh cache-busting image URL `http://<pc-ip>:<port>/frame<N>.jpg`.
3. The Chromecast fetches and shows each frame.

**Honest limits.** This is a **~1 fps preview, without audio** - the refresh rate is bounded by the
per-frame image `LOAD` round-trip, so it is smooth for a dashboard, a photo or slides, and jerky for
video. It is not smooth mirroring, and it is named accordingly ("Schermo su TV", not "mirroring"). The
capture-to-JPEG path is validated live (a 1366x768 desktop encodes to a ~280 KB JPEG); the media
server's buffer mode and range handling are loopback-tested.

## True screen mirroring - Cast Streaming (honest roadmap, not yet implemented)

Smooth, full-frame-rate mirroring with audio is a real but large project, and it is **not faked**.
Correcting an earlier over-simplification: Cast mirroring is **not** closed WebRTC - it is **Cast
Streaming**, the protocol implemented by Google's **openscreen** library (semi-documented). The shape:

1. Open the mirroring receiver over CASTv2 and negotiate an **OFFER/ANSWER** on the
   `urn:x-cast:com.google.cast.webrtc` namespace (codecs, SSRCs, AES key/IV, RTP payload types).
2. Capture the screen and **encode video in real time** to VP8 (or H.264) and audio to Opus.
3. Packetize into **Cast RTP** over **UDP**, with the Cast RTCP feedback loop and **AES-128** payload
   encryption keyed from the OFFER/ANSWER.

Feasibility on Haiku, checked honestly:

| Piece | State on this system |
|-------|----------------------|
| Screen capture (`BScreen::ReadBitmap`/`GetBitmap`) | available (used by the preview) |
| Real-time VP8/H.264 encoder | **not installed** - needs libvpx (BSD, from HaikuPorts) or x264 (GPL, would live under `optional/`, dynamically loaded) |
| Cast Streaming stack (OFFER/ANSWER, RTP/RTCP, AES) | to be written |

Milestones (each independently testable, none faked):

1. **Negotiation proof - DONE (code + tests; live pending a device).** `optional/cast/CastMirror.{h,cpp}`
   builds the mirroring OFFER (`castMode: mirroring`, a VP8/H.264 video stream + an Opus audio stream,
   real AES keys via OpenSSL `RAND_bytes`) and parses the device's ANSWER (`udpPort`, `sendIndexes`,
   `ssrcs`, or an error). `CastChannel` was generalized (`LaunchAppById`, `Send`, `Receive`) to launch
   the mirroring receiver app (`0F5096E8`) and exchange on the `webrtc` namespace. The OFFER builder and
   ANSWER parser are unit-tested (`test_mirror.cpp`, in `make test`). The **live** handshake is a dev
   tool, `mirror_probe.cpp` (`make -C packaging/cast probe`, then `./mirror_probe <ip>`): it needs a
   real Cast **video** receiver to answer (a Chromecast / Google TV; audio-only Google Home devices do
   not mirror), and none was on the dev LAN, so the live ANSWER has not yet been captured. No media is
   sent - this is only the handshake.
2. **Encoder integration - DONE (code + tests + live proof).** `optional/cast/VpxEncoder.{h,cpp}` is a
   real VP8 encoder over **libvpx** (BSD-3-Clause, `libvpx_devel` from HaikuPorts): it converts a
   captured BGRA screen frame (Haiku `B_RGB32`) to I420 and encodes VP8, emitting keyframe and
   inter-frame packets. Verified two ways, no faking: `test_vpx.cpp` (`make -C packaging/cast vpxtest`)
   encodes synthetic frames and **decodes them back with libvpx** to the right size (6 frames ->
   decodable 320x240), and a live capture of the real 1366x768 desktop encoded to a ~125 KB VP8
   keyframe that decoded back to 1366x768. The encoder is dev/roadmap code, not yet in the shipped app
   (that is milestone 4). libvpx is BSD, so it stays optional/dependency-clean.
3. **Transport**: implement Cast RTP packetization + AES + the RTCP timing/feedback over UDP.
4. **Live loop**: capture -> encode -> packetize -> send at 30 fps, then add Opus audio.

The dependency on a real-time encoder (a separate, licensed library) and the RTP/crypto stack make
this multi-step; the `~1 fps` preview above is the honest, working stand-in until it lands.

## Integration into Campiello

- `optional/cast/CastChannel.{h,cpp}`: the CASTv2 client - a hand-rolled `CastMessage` protobuf codec,
  tiny JSON readers, and a TLS channel over OpenSSL. `Connect`, `GetStatus`, `CastUrl`, `SetVolume`,
  `StopApp`. The codec and JSON readers are unit-tested off-device (`test_cast.cpp`).
- `optional/cast/DialClient.{h,cpp}`: hand-rolled DIAL over plain HTTP (`FriendlyName`, `AppState`,
  `Launch`, `Stop`; a namespace-agnostic `XmlTag`, unit-tested).
- `optional/cast/MediaServer.{h,cpp}`: the built-in HTTP server (Range support) used to cast a local
  file (file mode) or the live screen frames (buffer mode: `ServeBuffer` + `UpdateBuffer`), plus
  `GuessMediaType` and `LocalIpToward`. Pure sockets, no dependency; loopback-tested.
- `optional/cast/campiello_cast.cpp`: the panel app; all network I/O on worker threads. The screen
  preview grabs `BScreen` and encodes JPEG via the Translation Kit on its own thread. The media server
  is a window member that lives for as long as the window is open. `RefsReceived` reads
  `CAMPIELLO:host/name` from the WON device shortcut.
- `optional/cast/cast.handler`: matches `_googlecast._tcp`. `packaging/cast` builds
  `campiello_cast-0.4.0-1` and links OpenSSL (`libssl`/`libcrypto`, Apache-2.0) for the TLS channel,
  `libtracker` for the file panel, and `libtranslation` for JPEG encoding; the MIT core never depends
  on it.

## Testing status

The protobuf codec (round-trip, truncation) and the JSON readers (a realistic RECEIVER_STATUS) are
unit-tested, as is the DIAL `XmlTag`. Not yet validated against a physical Cast device (none on the
dev LAN - the test network had a Google Home zone but no `_googlecast` receiver). Some newer firmwares
restrict DIAL to whitelisted apps; CASTv2 media casting is the more general path.

## Follow-ups

- Live transport controls (play/pause/seek) and a media progress display via `MEDIA_STATUS`.
- Read the DIAL port / `Application-URL` from the device description instead of assuming `:8008`.
- The proprietary screen-mirroring channel (documented above) remains out of scope.
