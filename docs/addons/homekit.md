# campiello_homekit — HomeKit add-on

A Campiello device add-on (see `docs/DEVICE_ADDONS.md`) for an Apple HomeKit (HAP) accessory. It shows
the accessory's advertised info; it does **not** control it. Eleventh component of the suite in
`docs/ADDONS_SUITE.md`.

## What it is

HomeKit accessories (lights, locks, sensors, thermostats) announce themselves over mDNS as
`_hap._tcp` with a TXT record describing the accessory. Users want to see what an accessory is and
whether it is already paired.

## How it works

- The public info comes from the `_hap._tcp` **mDNS TXT** (no crypto): `md` (model/name), `id`
  (device id), `ci` (category id), `sf` (status flags; bit 0 set = not paired / discoverable), `c#`
  (config number), `s#` (state number), `pv` (HAP protocol version). Category ids map to kinds
  (5 = Lightbulb, 9 = Thermostat, 17 = IP Camera, ...).
- **Control** (read/write characteristics, e.g. turn a light on) requires a paired **secure session**:
  SRP pairing (`Pair-Setup` with the 8-digit code), long-term Ed25519 identities exchanged in
  `Pair-Verify`, then every HTTP message encrypted with ChaCha20-Poly1305. This is a large crypto
  module and a **documented follow-up**; it is NOT implemented here (no fake control).

## Integration into Campiello

- `optional/homekit/HapInfo.{h,cpp}`: a dependency-free decoder. `ParseHapTxt` turns TXT pairs into a
  `HapInfo` (name, category, pairing state, config, protocol); `CategoryName` maps `ci` to an Italian
  label. Unit-tested. No crypto, no network.
- `optional/homekit/campiello_homekit.cpp`: the info app. `RefsReceived` reads `CAMPIELLO:host/name`
  and the `CAMPIELLO:txt.<key>` attributes from the WON device shortcut, decodes them, and shows the
  accessory info plus a note that control needs HomeKit pairing (use Apple Home for now). Links only
  `libbe`.
- **WON plumbing**: `src/vicinato/ShareFolder.cpp` now writes each discovered mDNS TXT pair to the
  device shortcut as a `CAMPIELLO:txt.<key>` attribute (a generic enhancement, useful to every
  add-on), so add-ons get the advertised metadata without re-querying mDNS.
- `optional/homekit/homekit.handler`: matches `_hap._tcp`. `packaging/homekit` builds
  `campiello_homekit-0.1.0-1`.

## Licensing

No third-party code or library. The TXT keys and category ids are from the public HAP specification.
Fits the MIT core rule (kept under `optional/` only because it is device-specific).

## Reference material

- HomeKit Accessory Protocol specification (non-commercial) for the TXT keys and the pairing/session
  crypto.
- Apple's HomeKitADK, and `hap-python` / `HAP-NodeJS`, for the TXT keys and the category id table.

## Testing status

The TXT decoder and category map are unit-tested, and the `CAMPIELLO:txt.<key>` attribute plumbing is
verified end-to-end (a shortcut's attributes decode to the right accessory info). **Not validated
against a live accessory's control** because control is out of scope here.

## Follow-ups

- The full HAP client: SRP `Pair-Setup`, Ed25519 `Pair-Verify`, ChaCha20-Poly1305 secure session,
  then read/write characteristics (this would add a crypto library, kept in the optional set).
- A richer category-to-icon mapping in the WON list.
