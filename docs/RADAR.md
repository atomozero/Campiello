# Radar: an mDNS/DNS-SD network debug window

Status: **debug tool.** A window that opens the Bricola multicast socket, asks the network
"what services are out there?", and shows everything it hears live. It exists to answer, at a
glance, the question the discovery work keeps running into: *is anything reaching this machine
over multicast at all, and on which interface?* (docs/VERIFIED.md sections 11 and the open
items on multicast delivery.)

This is a diagnostic, not a product surface. It is not shipped in the core package.

## What it shows
- **Host che trasmettono**: every source IP that sends an mDNS datagram we receive, with packet
  and record counts and how long ago we last heard it. Zero rows here means no multicast is
  reaching us on the selected interface (the isolation case documented in VERIFIED.md).
- **Servizi**: every DNS-SD service type advertised on the LAN (`_campiello._tcp`, `_smb._tcp`,
  `_http._tcp`, printers, Bonjour/Avahi devices...), each expanding to its instances with
  host:port, resolved address, and TXT attributes.

## How it works (reuses the Bricola stack, nothing new on the wire)
```
MdnsSocket (existing)  ── open on a chosen interface, send/receive multicast
MdnsWire::Parse (existing) ── decode ANY mDNS packet into questions/records
MdnsRadar (new, portable)  ── aggregate sources + services + instances, thread + snapshot
campiello_radar (new, GUI) ── poll the snapshot on a timer, render an outline list
```
`MdnsRadar` sends the DNS-SD meta-query `_services._dns-sd._udp.local` (which asks every
responder to list its service types), then a PTR query per discovered type, and passively folds
in every SRV/TXT/A it sees. It is pure standard C++ + POSIX (no BeAPI), so its aggregation is
unit-tested off Haiku by feeding it packets built with MdnsWire.

## The interface picker (the point of the tool)
On Haiku there is no default route for 224.0.0.0/4, so multicast only works when bound to a
concrete interface (VERIFIED.md section 11). The window has an interface menu: **Automatica**
(the primary non-loopback IPv4), **127.0.0.1** (same-host only), and every local address. Switch
it to see exactly which interface carries traffic, which is the whole multicast-delivery
question made visible.

## Run it
```
cd tests/bricola && make campiello_radar && ./campiello_radar
```
Haiku-only (links libbe). The aggregator test (`make test_radar`) runs anywhere.
