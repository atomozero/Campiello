# campiello_alexa - Amazon Alexa add-on

A Campiello device add-on (see `docs/DEVICE_ADDONS.md`) for an Amazon Alexa device. It is
**informational only**: it shows the device's presence, but cannot control it. Fourteenth component of
the suite in `docs/ADDONS_SUITE.md`.

## What it is

Amazon Echo / Alexa devices announce themselves over mDNS as `_amzn-alexa._tcp`. Users might expect to
control them from the LAN.

## How it works (and why control is not possible)

There is **no open local control API** for Alexa. Alexa control goes through Amazon's cloud:

- The **Alexa Voice Service (AVS)** handles voice/audio between a device and the cloud (device-side,
  account-bound, not a LAN control surface).
- The **Smart Home Skill API** and the Alexa app control devices *through Amazon's servers*, using an
  Amazon account and OAuth - there is no documented way to send a command to an Echo directly over the
  local network.

So this add-on does the honest thing: it shows the device's presence and whatever it advertises over
mDNS, and explains that control needs the Amazon app/cloud. It performs no network requests and fakes
no control.

## Integration into Campiello

- `optional/alexa/campiello_alexa.cpp`: the info app. `RefsReceived` reads `CAMPIELLO:host/name` and
  the `CAMPIELLO:txt.<key>` attributes from the WON device shortcut and shows them, with a clear note
  that Alexa control is cloud/account only. No network, links only `libbe`.
- `optional/alexa/alexa.handler`: matches `_amzn-alexa._tcp`. `packaging/alexa` builds
  `campiello_alexa-0.1.0-1`.

## Licensing

No third-party code or library. Fits the MIT core rule (kept under `optional/` only because it is
device-specific).

## Reference material

- Amazon Alexa Voice Service (AVS) and Smart Home Skill API documentation - both are cloud/account
  APIs; neither exposes local device control, which is why this add-on is informational.

## Testing status

Compiles; shows the device presence and mDNS TXT. There is no protocol to validate because there is no
local control surface.

## Follow-ups

- None that are locally implementable. A cloud integration (AVS or Smart Home Skill, with an Amazon
  account and OAuth) would be a very different, account-bound feature, out of scope for a local
  neighborhood add-on.
