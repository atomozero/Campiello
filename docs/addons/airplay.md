# campiello_airplay — AirPlay add-on

A Campiello device add-on (see `docs/DEVICE_ADDONS.md`) for an AirPlay receiver. It shows the
receiver's info and capabilities; it does **not** stream or mirror. Sixteenth (last) component of the
suite in `docs/ADDONS_SUITE.md`.

## What it is

AirPlay receivers (Apple TV, HomePod, AirPlay speakers/TVs) announce themselves over mDNS as
`_airplay._tcp` (control/video) and `_raop._tcp` (audio/RAOP). Users want to see what a receiver is and
what it supports.

## How it works

- The public info comes from the mDNS **TXT** (no crypto): `model` (e.g. `AppleTV3,2`), `deviceid`,
  `srcvers` (AirPlay source version), `features`/`flags` (a capability bitfield), `pk` (public key),
  and `pw` (password required). The `features` value is either one hex number or two 32-bit words
  `0xLOW,0xHIGH` combined into a 64-bit bitfield; known bits map to capabilities (Video, Audio, Screen
  mirroring, AirPlay 2, ...).
- **Streaming/mirroring** needs the AirPlay 2 handshake (pair-setup / pair-verify with Curve25519 and
  Ed25519), then **FairPlay** and encrypted RTSP/RTP - substantial Apple crypto. That is a
  **documented follow-up**; it is NOT implemented here (no fake streaming).

## Integration into Campiello

- `optional/airplay/AirplayInfo.{h,cpp}`: a dependency-free decoder (same style as the HomeKit/Matter
  add-ons). `ParseFeatures` combines the one/two-word features string into 64 bits; `DecodeFeatures`
  maps a curated set of well-known bits to Italian labels; `ParseAirplayTxt` builds the summary.
  Unit-tested. No crypto, no network.
- `optional/airplay/campiello_airplay.cpp`: the info app. `RefsReceived` reads `CAMPIELLO:host/name`
  and the `CAMPIELLO:txt.<key>` attributes, decodes them, and shows the receiver info + capabilities
  with a note that streaming needs the AirPlay 2 handshake. Links only `libbe`.
- `optional/airplay/airplay.handler`: matches `_airplay._tcp` and `_raop._tcp`. `packaging/airplay`
  builds `campiello_airplay-0.1.0-1`.

## Licensing

No third-party code or library. The TXT keys and feature bits are from unofficial protocol docs and
open projects. Fits the MIT core rule (kept under `optional/` only because it is device-specific).

## Reference material

- The unofficial AirPlay protocol documentation, and the `pyatv` and OwnTone projects, for the TXT
  keys and the `features` bit meanings.

## Testing status

The features decoder is unit-tested (a two-word features string combines to the right 64-bit value and
decodes to Video/Audio/Screen mirroring/AirPlay 2). **Streaming is out of scope**, so there is nothing
live to validate beyond the info display.

## Follow-ups

- The AirPlay 2 client: pair-setup/pair-verify, FairPlay, and the audio/screen streaming path (adds
  Apple crypto), kept in the optional set.
