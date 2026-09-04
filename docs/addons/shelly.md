# campiello_shelly - Shelly relay/plug add-on

Controls a **Shelly** smart relay or plug discovered on the LAN, over its **local HTTP API** (no
cloud), for **both device generations**. It lists the device's channels with an on/off button and a
live power readout, and opens the device web interface.

## What it does

- **Auto-detects the generation** from `GET /shelly` (the `gen` field): gen1 devices omit it, gen2+
  (and gen3/4, which speak the same RPC dialect) report `gen>=2`.
- **gen1 (REST/CoIoT):** reads `GET /status` (`relays[].ison`, `meters[].power/total`, and a Shelly
  EM's `emeters[]`), controls with `GET /relay/<id>?turn=on|off|toggle`.
- **gen2+ (RPC-over-HTTP):** reads `GET /rpc/Shelly.GetStatus` (the `switch:N` blocks:
  `output`/`apower`/`voltage`/`aenergy.total`), controls with `GET /rpc/Switch.Set?id=<id>&on=true|false`
  and `Switch.Toggle`.
- Shows, per channel: state (On/Off) with a toggle button for controllable relays, and where the
  device meters power, a live **W / V / kWh** readout. A Shelly EM's clamp channels appear as
  **read-only** power meters.
- Energy units are normalized: gen2 `aenergy.total` and gen1 `emeters[].total` are Wh; a gen1 relay
  meter's `total` is watt-minutes and is converted to Wh (`/60`) so the kWh figure is correct.

Launched from the WON neighborhood on a double-click of a Shelly (the `shelly.handler` manifest),
which passes `CAMPIELLO:host`, `CAMPIELLO:name`, `CAMPIELLO:port` and the mDNS TXT as
`CAMPIELLO:txt.<key>`. It can also be run from the command line:
`campiello_shelly host=192.168.1.132 [name=Presa] [port=80] [gen=2]`.

## Discovery / routing

- **gen2+** devices advertise **`_shelly._tcp`**, matched directly by the handler.
- **gen1** relays/meters that only advertise the generic **`_http._tcp`** are still routed to the
  add-on by a **`match.txt`** rule in the handler framework: the manifest declares
  `match.txt = app=shelly` and `match.txt = id=shelly`, so a device whose TXT carries `app=shellyem`
  (or `id=shelly1-...`) is claimed - without the add-on hijacking every other web device on
  `_http._tcp`. This TXT-prefix match is a general handler-framework feature, not Shelly-specific.

## Implementation

- `optional/shelly/ShellyClient.{h,cpp}` is a dependency-light client: plain POSIX sockets speaking
  HTTP/1.0 (Shelly uses cleartext HTTP on port 80) plus a tiny JSON scanner. No libbe and no
  third-party library, so it is unit-testable off Haiku and the package stays **MIT-clean**. The
  status-body -> channel mapping is split into pure functions (`ParseChannelsGen1/2`) so it can be
  tested without a socket.
- `optional/shelly/campiello_shelly.cpp` is the BApplication/BWindow control panel; all network I/O
  runs on worker threads, results posted back as `BMessage`s. Links `libbe` + the network kit +
  `liblocalestub` (for `B_TRANSLATE`).

## Live watt graph + Desktop widget (replicant)

The control window draws a **rolling line chart of total active power (W)**: a lightweight
`PowerGraph` view keeping the last ~120 samples with an auto-scaled Y axis (full scale at the top,
0 W at the bottom); the current value is shown in the section title (`Potenza: N W`). A
`BMessageRunner` re-polls the device every 3 s (skipping a tick while a poll is already in flight),
so the graph moves on its own; an unreachable poll breaks the line instead of faking a value.

The graph can also live on the **Desktop as a replicant**. Press **Desktop** in the window: a small
holder opens with a draggable graph - grab the **handle in its bottom-right corner** and drag it onto
the Desktop, where it pins and re-polls on its own (reloaded from this app's image via the `add_on`
signature, class `ShellyGraphReplicant`). The replicant carries the host, port and generation in its
archive, so it survives a reboot. `PowerGraph` and the replicant share one `DrawWattHistory` routine
and reuse the same `RefreshThread`, so the on-Desktop graph stays consistent with the window.

The corner handle is only painted while Haiku's system-wide "show replicants" flag is on
(`BDragger::AreDraggersDrawn()`); if it is off the handle is invisible and the widget looks
undraggable. Opening the holder window turns the flag on (`BDragger::ShowAllDraggers()`) so the handle
is always there when you want to drag the graph out.

## Authentication

Auth is off by default on a freshly paired LAN device (`GET /shelly` reports `auth_en:false` on gen2,
`auth:false` on gen1). When it is enabled the calls return HTTP 401; the panel identifies the device
and shows a note that control may fail. **Digest authentication** (gen2) / basic auth (gen1) for
control is a documented follow-up, not faked.

## Testing status

- `test_shelly` passes (offline): the JSON scanners, generation detection, and the status->channel
  mapping for gen2 (`switch:0`), gen1 1PM (relay + Wmin meter -> Wh) and gen1 EM (relay + two Wh
  clamp meters). The wire bodies it parses match what real devices return.
- The **live** path is exercised by `test_shelly <host> [gen] [toggle]`. It was validated end to end
  against a real **Shelly Plus Plug S** (gen2, model `SNPL-00112EU`, firmware 1.7.5, `auth_en:false`):
  `FetchInfo` + `FetchChannels` returned the channel state and power/energy correctly. A Shelly EM
  (gen1) on the same LAN was offline at test time, so the gen1 live grab is covered by the offline
  parser test rather than a live capture.

## Follow-ups

- Digest/basic auth for control on locked-down devices.
- gen2 energy-meter profiles (`em:0`/`emdata:0` on a Pro EM / 3EM) and roller/cover and light/dimmer
  profiles (brightness, position).
- Surface the device-set channel names (from `/settings` gen1 or `Sys.GetConfig` gen2) instead of the
  generic "Canale N".
