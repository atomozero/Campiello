# campiello_nut - UPS monitor add-on (Network UPS Tools)

Handles a UPS exposed by a [Network UPS Tools](https://networkupstools.org) server, discovered on the
LAN via `_nut._tcp` (TCP 3493). It is a **monitor**: it reads the UPS state and lists every published
variable, but it is strictly read-only - it never sends an `INSTCMD`, `SET` or shutdown, so it cannot
change or power down the UPS. This matches Campiello's "look, do not touch" rule for shared
infrastructure.

## What it does

- Lists the units the server exposes (`LIST UPS`) and reads the first one's variables
  (`LIST VAR <ups>`).
- Highlights the essentials: status (on line / on battery / low battery / charging...), battery
  charge, estimated runtime, load and input voltage; and shows the full variable dump.

Launched from the WON neighborhood on a double-click of a UPS device (the `nut.handler` manifest),
which passes `CAMPIELLO:host`, `CAMPIELLO:name` and `CAMPIELLO:port`. It can also be run from the
command line: `campiello_nut host=192.168.1.100 [port=3493] [name=Sala macchine]`.

## Protocol

NUT is a line-oriented text protocol. The client sends `LIST UPS` / `LIST VAR <ups>` and the server
answers with `BEGIN LIST ... / UPS ... / VAR ... "value" / END LIST` blocks; values are double-quoted
with `"` and `\` backslash-escaped. `NutClient` is a hand-rolled read-only client + pure parsers
(`ParseListUps`, `ParseListVar`, plus `StatusText`/`RuntimeText` decoders), no third-party dependency,
so the parsers are unit-tested off Haiku (`test_nut.cpp`). The add-on sends `LOGOUT` after its query
so the server closes the connection cleanly.

## Validation

The parsers and the status/runtime decoders are unit-tested. Note that a NUT server often advertises
itself over mDNS while `upsd` is configured to `LISTEN 127.0.0.1` (localhost only) or behind an ACL,
so it may not accept LAN connections; the UPS discovered on the test network was not reachable for
that reason, so the live read path could not be exercised there. To expose a UPS to Campiello, `upsd`
must `LISTEN` on the LAN address and grant the client host in `upsd.conf`/`upsd.users`.

## Follow-ups

- Optional authentication (`USERNAME`/`PASSWORD`) for servers that require a login even to read.
- A periodic auto-refresh so the window tracks the UPS live during a power event.
- Read-only remains the intended scope; any control (`INSTCMD beeper.disable`, test start) would be a
  deliberate, separately-gated follow-up, never a default action.
