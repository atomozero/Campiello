# campiello_eero - eero mesh node add-on

Handles eero mesh Wi-Fi nodes discovered on the LAN via `_eero._tcp`. This is an **informational**
add-on: it shows the node's presence and the little it advertises over mDNS (its `base_mac`). It does
not control eero, because there is nothing on the LAN to control.

## Why info only (honest scope)

eero (an Amazon company) has **no open local control API**. All configuration - the network, client
devices, guest access, updates - goes through the eero mobile app talking to eero's cloud, bound to
an Amazon/eero account. There is no documented on-device REST/HTTP interface a LAN client can drive,
so a local controller is not possible; this add-on does not fake one.

What it does:

- Shows the node name, address and the advertised `base_mac`.
- Explains that management is done through the eero app.

Launched from the WON neighborhood on a double-click of an eero node (the `eero.handler` manifest),
which passes `CAMPIELLO:host`, `CAMPIELLO:name` and the mDNS TXT as `CAMPIELLO:txt.<key>`. No network,
no third-party dependency (links only libbe).

## Follow-ups

There is no honest control follow-up while eero keeps its control plane cloud-only and undocumented.
If eero ever ships a documented local API, a real controller could replace the info card; until then
this stays informational, like `campiello_alexa`.
