# campiello_daikin - Daikin air conditioner add-on

Handles Daikin air conditioners discovered on the LAN via `_dkapi._tcp` (the BRP069/BRP072 Wi-Fi
adapter, `adp_kind=3`). Unlike the info-only add-ons, this one does **real local control**: the
Daikin "aircon" adapter exposes an open, unauthenticated HTTP API on port 80, so Campiello reads the
live state and commands the unit directly - no cloud, no account, no crypto.

## What it does

- Reads the real state: indoor temperature, outdoor temperature, power, mode, target temperature,
  fan speed and direction (`/common/basic_info`, `/aircon/get_control_info`,
  `/aircon/get_sensor_info`).
- Controls the unit: power on/off, mode (auto / cool / heat / dehumidify / fan), target temperature
  (0.5 C steps, clamped 16-30 C) and fan speed - all via `/aircon/set_control_info`.

Launched from the WON neighborhood on a double-click of a Daikin device (the `daikin.handler`
manifest), which passes `CAMPIELLO:host`, `CAMPIELLO:name` and `CAMPIELLO:port`. It can also be run
from the command line: `campiello_daikin host=192.168.1.90 [port=80] [name=Sala]`.

## Protocol

Plain HTTP, text responses of the form `ret=OK,key=value,key=value,...` (some values percent-encoded,
e.g. the unit name). `DaikinClient` is a hand-rolled client + `ParseResponse` codec, no third-party
dependency, so the parser is unit-tested off Haiku (`test_daikin.cpp`).

Key fields:

- `pow` - 0 off / 1 on.
- `mode` - `0`/`1`/`7` auto, `2` dehumidify, `3` cool, `4` heat, `6` fan.
- `stemp` - target temperature (a number in cool/heat/auto; the marker `M`/`--` in fan/dehumidify,
  where there is no target).
- `shum` - target humidity.
- `f_rate` - `A` auto, `B` silent, `3`..`7` fixed levels.
- `f_dir` - `0` stop, `1` vertical, `2` horizontal, `3` both.

The set endpoint is **stateful**: the adapter expects the full `pow`/`mode`/`stemp`/`shum` set on
every call, so the add-on reads the current control info, changes the one field the user touched, and
resends the whole set. When switching to fan or dehumidify it sends `stemp=M` (those modes reject a
numeric target). A mode change is sent with `pow=1` so selecting a mode on a powered-off unit turns
it on rather than being a no-op.

## Validation

The client was validated live against two real BRP069 units (firmware `4_2_303`): it read power,
mode, target and the indoor/outdoor sensors correctly. The read path and the parser are exercised;
the set path uses the same documented endpoint. References (behavioural, not code): `pydaikin`,
`ael-code/daikin-control`.

## Follow-ups

- The BRP072C "SkyFi" variant and newer adapters use a different, token-authenticated API
  (`/aircon/get_control_info` under a UUID/key); this add-on targets the classic BRP069 API.
- Reading and setting timer/schedule and the special modes (powerful, econo, streamer) exposed by
  `/aircon/get_control_info`'s extended fields.
- `_dkapi` over TLS where a newer adapter advertises it.
