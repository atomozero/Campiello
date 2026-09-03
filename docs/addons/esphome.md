# campiello_esphome - ESPHome node add-on

Handles ESPHome nodes discovered on the LAN via `_esphomelib._tcp`. ESPHome is the open-source
firmware for ESP8266/ESP32 devices (widely used with Home Assistant). This add-on **identifies** the
device, opens its web interface, and for an **ESP32-CAM** node **streams the live camera feed** in a
native view. It does not yet drive arbitrary entities/sensors, because that needs the ESPHome native
API (see follow-up).

## What it does

- Shows the device info carried in the mDNS TXT record: `project_name`/`project_version` (the user's
  firmware project), `version` (ESPHome version), `board`, `platform`, `network` and `mac`.
- Opens the device's web interface (`http://<host>/`) via the registered browser, when the ESPHome
  `web_server` component is enabled.
- **Live camera streaming** for ESP32-CAM nodes (see below).

Launched from the WON neighborhood on a double-click of an ESPHome device (the `esphome.handler`
manifest), which passes `CAMPIELLO:host`, `CAMPIELLO:name` and the mDNS TXT as `CAMPIELLO:txt.<key>`.
It can also be run from the command line: `campiello_esphome host=192.168.1.91 [name=fancoil_01]`,
or straight to the camera: `campiello_esphome host=192.168.1.91 video=1 [port=8080]`.

## Live camera (ESP32-CAM)

An ESP32-CAM running ESPHome's `esp32_camera_web_server` exposes video over plain HTTP, independent of
the native API: a **Motion-JPEG** stream (`Content-Type: multipart/x-mixed-replace`, each part a JPEG
with its own `Content-Length`) on one port, and a still **snapshot** (`image/jpeg`) on another. The
common defaults are **8080** (stream) and **8081** (snapshot); a real device confirmed this exact wire
format.

- `optional/esphome/MjpegStream.{h,cpp}` is a dependency-light socket client: it opens the stream,
  reads the boundary and per-part `Content-Length`, and yields raw JPEG frames (plus a one-shot
  `Snapshot()`). No libbe, so it is unit/integration-tested (`test_mjpeg.cpp`).
- In the add-on, a **camera node is detected** from `board` (`esp32cam`) or a device name containing
  "camera"/"telecamera", which shows a **"Guarda video"** button. The camera window runs a reader
  thread that decodes each JPEG with the **Haiku Translation Kit** (`BTranslatorRoster` ->
  `BBitmap`) and blits it letterbox-scaled into a `BView`; the stream port is editable (default 8080)
  with a **Riconnetti** button, and the status line shows a live fps.
- No MJPEG-only firmware, no native API, no Noise: this is why camera video works today while general
  entity control is still a follow-up. Links `libbe`, the network kit and `libtranslation` (JPEG
  decode); no third-party dependency, so the add-on stays MIT-clean.

## Why info + web, not control (yet)

ESPHome's real control channel is its **native API**: a length-prefixed protobuf stream on TCP
**6053** (the port the mDNS SRV record advertises). It supports a plaintext handshake
(`HelloRequest`/`HelloResponse`, then `ConnectRequest` with an optional password, `ListEntitiesRequest`,
`SubscribeStatesRequest`, and per-entity command messages), and increasingly a **Noise**-encrypted
transport keyed by a base64 API key. Implementing it honestly means a protobuf codec plus the entity
model - a real follow-up, deliberately not faked here. The web UI covers the common "flip a switch,
read a sensor" need in the meantime.

## Desktop widget (replicant)

The camera can live on the Desktop as a **replicant**. In the camera window press **Desktop**: a small
holder window opens with a draggable view - drag its bottom-right corner onto the Desktop and it pins
there, reconnecting on its own (reloaded from this app's image via the `add_on` signature, class
`EsphomeCameraReplicant`). A click on the widget switches between **live video** and **snapshot every
2s**. Snapshot is the default: light, coexists with other viewers (e.g. Home Assistant), and survives
Wi-Fi hiccups - a continuous stream instead holds the camera's single connection. The replicant carries
the host and both ports in its archive, so it survives a reboot.

## Testing status

- `test_mjpeg` passes: the multipart parser extracts frames from a synthetic buffer (Content-Length
  driven), and `BoundaryFromContentType` handles quoted/unquoted boundaries. The wire format it parses
  was captured from a **real ESP32-CAM** (`192.168.188.99:8080`, board `esp32cam`, ESPHome 2024.3.1):
  `multipart/x-mixed-replace` + per-frame `image/jpeg`/`Content-Length`, snapshot on `:8081`.
- The **live end-to-end** view (connect, decode, blit) is exercised by
  `test_mjpeg <host> [stream_port] [snapshot_port]` and `campiello_esphome host=<ip> video=1`; run it
  against a powered-on camera to confirm frames render. It could not be re-run headless here because
  that camera was offline at build time.

## Follow-ups

- A native-API client (protobuf over 6053): Hello/Connect, list entities, subscribe to states, and
  send commands (switch, light, climate, number...). Start with the plaintext transport, then add
  Noise for API-key-protected nodes.
- A snapshot-only fallback (port 8081) and a "save frame" action; auto-probe the stream port when it
  is not the 8080 default.
- Surface the advertised `friendly_name`/entities in the window instead of just the raw TXT.
- Home Assistant is the other consumer of this API; the message set is stable and documented in the
  `esphome`/`aioesphomeapi` projects (references, not code).
