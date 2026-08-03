# campiello_esphome - ESPHome node add-on

Handles ESPHome nodes discovered on the LAN via `_esphomelib._tcp`. ESPHome is the open-source
firmware for ESP8266/ESP32 devices (widely used with Home Assistant). This add-on **identifies** the
device from what it advertises and opens its web interface; it does not yet drive entities/sensors,
because that needs the ESPHome native API (see follow-up).

## What it does

- Shows the device info carried in the mDNS TXT record: `project_name`/`project_version` (the user's
  firmware project), `version` (ESPHome version), `board`, `platform`, `network` and `mac`.
- Opens the device's web interface (`http://<host>/`) via the registered browser, when the ESPHome
  `web_server` component is enabled.

Launched from the WON neighborhood on a double-click of an ESPHome device (the `esphome.handler`
manifest), which passes `CAMPIELLO:host`, `CAMPIELLO:name` and the mDNS TXT as `CAMPIELLO:txt.<key>`.
It can also be run from the command line: `campiello_esphome host=192.168.1.91 [name=fancoil_01]`.

The add-on itself needs no network and no third-party dependency: it reads BFS attributes and hands
the web URL to `be_roster`.

## Why info + web, not control (yet)

ESPHome's real control channel is its **native API**: a length-prefixed protobuf stream on TCP
**6053** (the port the mDNS SRV record advertises). It supports a plaintext handshake
(`HelloRequest`/`HelloResponse`, then `ConnectRequest` with an optional password, `ListEntitiesRequest`,
`SubscribeStatesRequest`, and per-entity command messages), and increasingly a **Noise**-encrypted
transport keyed by a base64 API key. Implementing it honestly means a protobuf codec plus the entity
model - a real follow-up, deliberately not faked here. The web UI covers the common "flip a switch,
read a sensor" need in the meantime.

## Follow-ups

- A native-API client (protobuf over 6053): Hello/Connect, list entities, subscribe to states, and
  send commands (switch, light, climate, number...). Start with the plaintext transport, then add
  Noise for API-key-protected nodes.
- Surface the advertised `friendly_name`/entities in the window instead of just the raw TXT.
- Home Assistant is the other consumer of this API; the message set is stable and documented in the
  `esphome`/`aioesphomeapi` projects (references, not code).
