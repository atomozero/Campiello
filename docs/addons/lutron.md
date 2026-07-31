# campiello_lutron — Lutron add-on

A Campiello device add-on (see `docs/DEVICE_ADDONS.md`) for a Lutron Caseta / RA smart bridge. It
shows the device presence and its LEAP connection info; it does **not** control it. Fifteenth
component of the suite in `docs/ADDONS_SUITE.md`.

## What it is

Lutron smart bridges (Caseta, RA2/RA3) control lights, dimmers, and shades, and advertise themselves
over mDNS as `_sleap._tcp` (Secure LEAP). Users want to see the bridge and control the devices.

## How it works

- **Secure LEAP** runs over **TLS on port 8081**. To control anything, a client must first **pair**:
  the user presses the button on the bridge, the client generates a certificate signing request, and
  the bridge issues a **client certificate**. Afterwards the client opens a TLS connection using that
  client certificate and exchanges **LEAP** messages (JSON with `CommuniqueType`, `Header.Url`, `Body`)
  to read the device list and set levels.
- That pairing + mutual-TLS + LEAP message layer is a **heavy follow-up** (docs below) and requires a
  TLS stack with client certificates. It is NOT implemented here (no fake control).

So this add-on shows the bridge presence, its `host:8081` LEAP endpoint, and whatever it advertises
over mDNS, and explains that control needs the client-certificate pairing.

## Integration into Campiello

- `optional/lutron/campiello_lutron.cpp`: the info app. `RefsReceived` reads `CAMPIELLO:host/name`,
  the SRV port from `CAMPIELLO:port` (falls back to 8081), and the `CAMPIELLO:txt.<key>` attributes,
  and shows the bridge info with a note about the LEAP pairing. No network, links only `libbe`.
- `optional/lutron/lutron.handler`: matches `_sleap._tcp`. `packaging/lutron` builds
  `campiello_lutron-0.1.0-1`.

## Licensing

No third-party code or library. Fits the MIT core rule (kept under `optional/` only because it is
device-specific). A future LEAP client would link a TLS library (OpenSSL, Apache-2.0), staying in the
optional set.

## Reference material

- `gurumitts/pylutron-caseta` (the reference open Python LEAP client) for the pairing flow, the TLS
  client-certificate enrollment, and the LEAP message format.
- Lutron's LEAP is documented via that project and community reverse-engineering; there is no public
  Lutron spec.

## Testing status

Compiles; shows the bridge presence and LEAP endpoint. There is no unauthenticated data to fetch (LEAP
requires the paired client certificate), so there is nothing live to validate beyond the info display.

## Follow-ups

- The LEAP client: button-press pairing (CSR -> client certificate), a mutual-TLS connection, and the
  LEAP request/response messages to list zones and set light/shade levels. This adds a TLS library and
  is a substantial module, kept in the optional set.
