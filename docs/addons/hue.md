# campiello_hue — Philips Hue add-on

A Campiello device add-on (see `docs/DEVICE_ADDONS.md`) that controls a Philips Hue bridge over its
local REST API. No cloud, no vendor SDK. First add-on of the suite in `docs/ADDONS_SUITE.md`.

## What it is

A Hue bridge is a small hub on the LAN that controls Zigbee lights, plugs, and sensors. It advertises
itself over mDNS as `_hue._tcp` and exposes a documented local REST API. Users want to turn lights on
and off and set brightness/colour from their computer.

## How it works (protocol)

The bridge serves HTTPS on port 443 with a self-signed certificate (a LAN device authorized by a
physical button), so the client does not verify the certificate.

- **Discovery**: mDNS `_hue._tcp` (Campiello already classifies this as `ServiceKind::Home`). The
  bridge id is also in the TXT record. The IP comes from the resolved host.
- **Pairing** (v1 endpoint, still the way to mint a v2 key):
  `POST https://<ip>/api` with `{"devicetype":"campiello#haiku","generateclientkey":true}`.
  The user must press the round link button on the bridge first, otherwise the reply is
  `[{"error":{"type":101,"description":"link button not pressed"}}]`. On success the reply is
  `[{"success":{"username":"<application-key>","clientkey":"<hex>"}}]`. The `username` is the
  **application key** used for every later call.
- **List lights** (v2): `GET https://<ip>/clip/v2/resource/light` with header
  `hue-application-key: <application-key>`. The JSON has `data:[ {light}, ... ]`; each light has its
  own `id` (a UUID used in control paths), `metadata.name`, `on.on`, and `dimming.brightness` (0-100).
- **Control** (v2): `PUT https://<ip>/clip/v2/resource/light/<id>` with the same header and a body:
  `{"on":{"on":true}}` to switch, `{"dimming":{"brightness":75}}` to dim, `{"color":{"xy":{...}}}`
  for colour (colour is a planned follow-up).

## Integration into Campiello

- `optional/hue/HueBridge.{h,cpp}`: the protocol client. TLS/socket handling mirrors
  `optional/firetv/FireTVRemote.cpp` (self-signed LAN device); dependency-free JSON helpers parse the
  small responses (`SplitJsonDataObjects` splits the `data` array brace-aware).
- `optional/hue/campiello_hue.cpp`: the control-panel app. Pairing panel, then a light list with an
  on/off button and a brightness slider per light. All network I/O runs on worker threads
  (`PairThread`/`ListThread`/`SetThread`) so the UI never blocks. The application key is stored per
  host in `<settings>/Campiello/hue_keys` (owner-only); a later step can move it to the encrypted
  secret store the SMB helper uses.
- `optional/hue/hue.handler`: matches `_hue._tcp`; `RefsReceived` reads `CAMPIELLO:host/name` from the
  WON device shortcut. Launched by a double-click of a Hue bridge in `~/WON`.
- `packaging/hue/`: builds `campiello_hue-<v>.hpkg` (app + manifest). Requires `libssl`/`libcrypto`
  (Apache-2.0); the MIT core is unaffected.

## Licensing

OpenSSL (Apache-2.0) is allowed and used only in this optional package. No Hue vendor SDK or GPL/LGPL
code is used. The protocol is implemented from the public developer documentation.

## Reference code (harvested for the protocol, not the implementation)

- `github.com/tigoe/hue-control` — plain, well-commented instructions on the local bridge API
  (pairing, light control) used to confirm the endpoints and request/response shapes.
- Community clients (`Q42.HueApi`, `python-hue-v2`) corroborate the v2 `clip/v2/resource/light`
  paths, the `hue-application-key` header, and the link-button pairing error (type 101).

## Testing status

Compiles and follows the verified protocol. **Not yet validated against a physical bridge** (none
available in the dev environment). To validate: run `campiello_hue host=<bridge-ip>`, press the link
button, click "Abbina", then toggle a light. Live validation is tracked in `docs/ADDONS_SUITE.md`.
