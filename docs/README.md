# Campiello - documentation overview

Campiello is a native Haiku network neighborhood and file-sharing application: a
folder on the Desktop where every machine and service found on the local network
appears as an icon, and a double-click enters it. It is the modern, native
successor to the BeOS "World O' Networking" (WON) experience, rebuilt on modern
Haiku without Samba and without GPL or LGPL code in its core.

The name comes from the small Venetian square where neighbors gather - the literal
sense of "network neighborhood". The core application is MIT-licensed; anything
that would compromise that (a vendor SDK, an LGPL/GPL library) is confined to an
optional, separately installed add-on that is dynamically loaded and off by
default.

## Guiding principles

Three requirements rank above feature breadth, and a milestone is not considered
done if it violates one:

1. **Install in a double click.** One hpkg, no terminal, no config files, no
   services to start by hand.
2. **First use at zero configuration.** Peers appear by themselves and you enter
   them with a double-click. No IP address, no mount command, no manual.
3. **Security that is strong but invisible.** Real authentication and encryption
   under the hood, with the user never meeting a key, a certificate, or a CA - the
   only thing they ever see is a one-tap "allow this computer" prompt.

Developer and maintenance documentation is written in English; end-user strings in
the shipped applications are Italian. Core dependencies stay permissive
(MIT/BSD/Apache/ISC/zlib/public-domain); every network response is treated as
untrusted input.

## Capability areas

### The WON application and network neighborhood

The `campiello_won` / `campiello_vicinato` application discovers the LAN over
mDNS/DNS-SD, classifies each service by kind, and presents the result both as a
browsable window and as a live `~/WON` folder of per-service shortcuts. A
double-click enters a browsable service, opens a login helper for a protected one,
or shows an information card for a device that is not a file share. It offers
per-device actions (open, web UI, copy IP, inspect, open an SSH terminal, launch
an RDP client, Wake-on-LAN), reachability status via TCP and ARP, and a details
panel built from the decoded service metadata. See `NEIGHBORHOOD.md`.

### Interop mounts and the native protocol

Remote shares from the rest of the world mount as read-only Tracker volumes
through a userlandfs FUSE front end over a common `PeerBackend` abstraction: SFTP
over SSH (`M1.md`) and Windows SMB via libsmb2 in an optional package (`SMB.md`).
Alongside interop, the Campiello Native Protocol (CNP) is a TLS 1.3, mutually
authenticated, SPKI-pinned wire protocol for Haiku-to-Haiku sharing that preserves
BFS extended attributes and MIME types end to end, with distributed live queries
as its distinctive goal (`PROTOCOL.md`). A discovery filesystem that presents the
live peer set as a self-updating Tracker folder is designed in `DISCOVERY_FS.md`.

### NetIntel enrichment

A background, dependency-free pass enriches the neighborhood with passive LAN
intelligence: manufacturer and MAC from the ARP table and the IEEE OUI database,
NetBIOS computer names for SMB hosts, SSDP/UPnP devices that mDNS misses, and
Wake-on-LAN for any device whose MAC has been learned. See `NETINTEL.md`.

### The optional device add-on suite

Each recognizable non-file service can become an optional device add-on: a
separate application, matched by a `*.handler` manifest and launched from `~/WON`
on a double-click, that adds real interaction for one class of device without
bloating the MIT core. The suite spans smart-home, media, printing, scanning and
file services (Philips Hue, IPP, eSCL, FTP, WebDAV, NFS, AFP, DAAP, VNC, Daikin,
NUT, and more), plus honest information-only add-ons where no open local protocol
exists. The design is in `DEVICE_ADDONS.md`, the master plan and progress log in
`ADDONS_SUITE.md`, and a per-add-on developer note lives under `addons/`.

### The Google Cast add-on

`campiello_cast` grew from a DIAL launcher into a real Cast control channel over
the CASTv2 protobuf protocol (TLS 8009): it reads the device state, casts a media
URL into the Default Media Receiver, and offers a volume control, with DIAL as a
fallback. It further implements live screen mirroring, including audio, over the
Cast remoting channel. See `addons/cast.md`.

## Document index

| Document | What it covers |
|----------|----------------|
| [`PROPOSAL.md`](PROPOSAL.md) | The driving design context: mission, scope, verified ground truth, milestones. |
| [`VERIFIED.md`](VERIFIED.md) | Facts checked against real Haiku and reference-project source, with paths. |
| [`REUSE.md`](REUSE.md) | What Campiello can harvest from sibling Haiku projects, and what is greenfield. |
| [`PROTOCOL.md`](PROTOCOL.md) | The Campiello Native Protocol (CNP) wire specification for native mode. |
| [`M1.md`](M1.md) | Interop read-only mounts: SFTP over the FUSE front end. |
| [`SMB.md`](SMB.md) | Windows SMB/CIFS interop via libsmb2 in an optional package. |
| [`NEIGHBORHOOD.md`](NEIGHBORHOOD.md) | The WON-style network neighborhood on the Desktop (the `campiello_won` app). |
| [`DISCOVERY_FS.md`](DISCOVERY_FS.md) | Design for a discovery filesystem that presents live peers as a Tracker folder. |
| [`NETINTEL.md`](NETINTEL.md) | Passive LAN intelligence: ARP/OUI, NetBIOS, SSDP/UPnP, Wake-on-LAN. |
| [`RADAR.md`](RADAR.md) | The mDNS/DNS-SD debug window used to diagnose multicast delivery. |
| [`ICONS.md`](ICONS.md) | Per-service HVIF vector icons and the optional icon package. |
| [`DEVICE_ADDONS.md`](DEVICE_ADDONS.md) | The device add-on framework: handler manifests, matching, launch protocol. |
| [`ADDONS_SUITE.md`](ADDONS_SUITE.md) | Master plan and progress log for the full device add-on suite. |
| [`addons/`](addons/) | Per-add-on developer notes (one file per device add-on). |

## Repository layout

Developer notes reference the source tree they document. In broad strokes: the
discovery and mDNS engine lives under `src/bricola/`, the neighborhood application
and its classifier under `src/vicinato/`, the FUSE front end and interop backends
under `src/fondamenta/`, the native protocol under `src/traghetto/`, optional
add-ons and packaging under `optional/` and `packaging/`. Each add-on ships as its
own hpkg so the MIT core carries no optional dependency.
