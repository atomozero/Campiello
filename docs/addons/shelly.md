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

The graph can also live on the **Desktop as a replicant**. The in-window graph *is* an archivable
`ShellyGraphReplicant` with a drag handle in its bottom-right corner: grab that handle and drag it
straight onto the Desktop, where a copy pins and re-polls on its own (reloaded from this app's image
via the `add_on` signature, class `ShellyGraphReplicant`). There is no separate window or button - the
handle is right on the graph. The replicant carries the host, port and generation in its archive, so
it survives a reboot.

The same class serves both places: while embedded in the control window it does not poll (the window's
poll feeds it via `Push()`), and when reconstructed from an archive on the Desktop it self-polls with
a `BMessageRunner` on the same `RefreshThread`. Both draw through one `DrawWattHistory` routine, so the
on-Desktop graph stays consistent with the window.

Two Haiku details make this work. The corner handle is only painted while the system-wide "show
replicants" flag is on (`BDragger::AreDraggersDrawn()`); the window turns it on
(`BDragger::ShowAllDraggers()`) so the handle is visible without the user enabling it from the Deskbar.
And `ShellyGraphReplicant::Instantiate` is defined out-of-line (so `-O2` cannot drop the symbol the
shelf looks up by name) with the class archived under its real global-namespace name, otherwise a
dropped replicant comes back as a grey zombie box.

## 48h history + background logger

The graph has two modes, switched by a **Live / 48h** button in the window (and by a click on an
on-Desktop replicant): **Live** is the rolling last-~120-samples chart described above, and **48h**
draws the **last 48 hours** of total active power read from a local log. A Shelly gen2 Switch exposes
only the instantaneous `apower` and the cumulative `aenergy.total`; it keeps **no on-device power
history**, so the 48h view is drawn from a log this add-on records itself.

That log is written by **`campiello_shelly_logd`**, a headless background logger started at login by
its own launch_daemon job (`data/user_launch/campiello_shelly`, signature
`x-vnd.Campiello-shelly-logd`). Once a minute it reads a **watch list** of the plugs the user has
opened in the control panel, polls each one's total active power over the same local HTTP API, and
appends one `epoch watts` sample to that device's log, trimmed to the last 48 hours (~2880 samples).
Because it runs whether or not a window is open, the 48h graph has real coverage even after a reboot.

- **Files** live under `$CAMPIELLO_SHELLY_DIR` or `$HOME/config/settings/Campiello/shelly`: one
  `<host>.log` per device (`epoch watts\n` lines) plus a single `watch` list
  (`host<TAB>port<TAB>gen<TAB>name\n`). `ShellyPowerLog` (`optional/shelly/ShellyPowerLog.{h,cpp}`) is
  the sole helper: `Append`/`Load`/`Trim` for samples, `LoadWatch`/`AddWatch` for the watch list. It
  is dependency-free (no libbe, no third-party library), so the logger links only the socket client
  and the log helper and the package stays **MIT-clean**.
- The control panel is the **reader and the registrar**: opening a Shelly calls `AddWatch` (deduped by
  host) so the logger picks it up, and switching to 48h calls `ReloadHistory` to load
  `ShellyPowerLog::Load(host, now - 48h)`. The logger is the **single writer**; the GUI never writes
  the sample logs.
- Kept deliberately **out of `campiello_daemon`**: the core stays device-agnostic and MIT-clean, so
  the Shelly-specific polling lives in this add-on's own logger rather than the shared file-sharing
  node.

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
- `test_powerlog` passes (21 checks, offline): `ParseLog` (valid/junk lines, `since` filtering),
  `SanitizeHost`, `ParseWatch` (fields, defaults, names with spaces, comments), a filesystem
  round-trip in a scratch dir (append + load-since + 48h `Trim` + `AddWatch` dedup/update), and the
  NaN reject. Build with `g++ -std=c++17 -o test_powerlog test_powerlog.cpp ShellyPowerLog.cpp`.

## Follow-ups

- Digest/basic auth for control on locked-down devices.
- gen2 energy-meter profiles (`em:0`/`emdata:0` on a Pro EM / 3EM) and roller/cover and light/dimmer
  profiles (brightness, position).
- Surface the device-set channel names (from `/settings` gen1 or `Sys.GetConfig` gen2) instead of the
  generic "Canale N".
