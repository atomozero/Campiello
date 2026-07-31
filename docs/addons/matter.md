# campiello_matter — Matter add-on

A Campiello device add-on (see `docs/DEVICE_ADDONS.md`) for a Matter device. It shows the device's
advertised info; it does **not** commission or control it. Twelfth component of the suite in
`docs/ADDONS_SUITE.md`.

## What it is

Matter (Connectivity Standards Alliance) is the cross-vendor smart-home standard. Devices announce
themselves over mDNS as `_matterc._udp` while commissionable and `_matter._tcp` once operational, with
a TXT record describing the device. Users want to see what a Matter device is and whether it is in
pairing mode.

## How it works

- The public info comes from the mDNS **TXT** (no crypto): `D` (discriminator), `VP` (vendor+product,
  `vendor+product` in decimal), `CM` (commissioning mode: 0 no, 1 standard, 2 enhanced), `DT` (device
  type id), `DN` (device name), `SII`/`SAI`/`SAT` (session idle/active/threshold intervals), `T` (TCP
  supported). Device type ids map to kinds (257 = Dimmable Light, 769 = Thermostat, 10 = Door Lock, ...).
- **Commissioning and control** need the Matter secure sessions (**PASE** from the setup passcode,
  then **CASE** with operational certificates), device **attestation certificates**, and Thread/Wi-Fi
  onboarding. That is the Matter SDK (connectedhomeip) with substantial crypto, and a **documented
  follow-up** - it is NOT implemented here (no fake control).

## Integration into Campiello

- `optional/matter/MatterInfo.{h,cpp}`: a dependency-free decoder (same style as the HomeKit add-on).
  `ParseMatterTxt` turns TXT pairs into a `MatterInfo` (vendor/product, device type, commissioning
  state, discriminator, session timing); `DeviceTypeName` maps `DT` to an Italian label. Unit-tested.
  No crypto, no network.
- `optional/matter/campiello_matter.cpp`: the info app. `RefsReceived` reads `CAMPIELLO:host/name` and
  the `CAMPIELLO:txt.<key>` attributes from the WON device shortcut (plumbed by `ShareFolder` since the
  HomeKit iteration), decodes them, and shows the device info plus a note that commissioning/control
  needs the Matter secure sessions. Links only `libbe`.
- `optional/matter/matter.handler`: matches `_matter._tcp` and `_matterc._udp`. `packaging/matter`
  builds `campiello_matter-0.1.0-1`.

## Licensing

No third-party code or library. The TXT keys and device type ids are from the public Matter Core
Specification. Fits the MIT core rule (kept under `optional/` only because it is device-specific).

## Reference material

- Matter Core Specification (CSA) for the commissionable/operational TXT keys and the device types.
- `project-chip/connectedhomeip` (the Matter SDK) and `home-assistant-libs/python-matter-server` for
  the TXT keys and the device type id table.

## Testing status

The TXT decoder and device-type map are unit-tested (a synthetic commissionable TXT decodes to the
right vendor/product, device type, commissioning state, and session timing). The
`CAMPIELLO:txt.<key>` attribute plumbing was verified end-to-end in the HomeKit iteration. **Control
is out of scope here**, so there is nothing live to validate beyond the info display.

## Follow-ups

- The full Matter client: PASE/CASE, attestation, Thread/Wi-Fi onboarding, then the interaction model
  (read/subscribe/invoke). This is a large module built on the Matter SDK, kept in the optional set.
- A device-type-to-icon mapping in the WON list.
