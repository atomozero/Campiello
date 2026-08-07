# Campiello

A modern, native, MIT-licensed successor to World O' Networking for
[Haiku](https://www.haiku-os.org): the machines on your LAN appear as browsable folders
inside Tracker, with no Samba, no config files, and no visible keys or certificates.

![The WON network neighborhood](docs/screenshots/won-main.png)

If Campiello is useful to you, consider supporting development: [![Buy Me A Coffee](https://img.shields.io/badge/Buy%20Me%20A%20Coffee-atomozero-yellow?logo=buymeacoffee)](https://buymeacoffee.com/atomozero)

Three non-negotiable principles: install in a double click, first use at zero
configuration, and security that is strong under the hood but invisible on the surface
(only ever a one-tap "allow this computer" prompt).

## Features

* Native Haiku app and daemon - no Samba, no config files, just the system kits,
  OpenSSL, and libfuse
* **WON** (World O' Networking) network-neighborhood app: every service on the LAN
  (Windows shares, Campiello peers, computers, printers, media devices) grouped by
  kind, each with its own vector icon
* Live **online / offline** status per device, from a TCP probe plus ARP reachability,
  so a silent wifi router still shows as present
* **LAN enrichment (NetIntel)**: manufacturer and MAC from the ARP/OUI table, NetBIOS
  name, and SSDP/UPnP model, gathered in the background
* **One-click actions** per device: open, Web UI, SSH-in-a-Terminal, RDP remote
  desktop, Wake-on-LAN, and a collapsible network-details panel
* Two modes, one browser:
  * **Interop** mounts remote shares from the rest of the world - SFTP now (read-only by
    default, with an opt-in read-write mount), Windows/SMB via the optional add-on - so Haiku
    talks to Windows, Linux, macOS, and NAS boxes
  * **Native** is a Campiello-to-Campiello protocol (CNP) with full BFS attribute
    fidelity over **mutual TLS 1.3**
* **Distributed live query** - the headline native feature: a query predicate fans out
  to every peer, the matches appear in a virtual `/.query/<predicate>` folder, and they
  update live as files change on the peers. No SMB or NFS stack carries this
* **Zero-config discovery** over mDNS/DNS-SD (`_campiello._tcp`), feeding a Deskbar
  replicant that shows peer presence
* **Invisible security**: pairing is a single "allow this computer" prompt; keys and
  certificates are never shown, and identity is a TLS fingerprint that cannot be spoofed
  by renaming
* **Radar**: an mDNS/DNS-SD debug window that decodes exactly what is on the network
* Optional **device add-on suite** that turns a discovered device into a real action
  (Philips Hue, Chromecast, printers/scanners, and more)
* Optional **Windows/SMB** shares via libsmb2 (LGPL, kept out of the MIT core)
* **Localized** through the Haiku Locale Kit: the WON app and every device add-on ship
  Italian and English catalogs and follow the user's language, with Italian as the fallback
* MIT licensed, with no external dependencies in the core beyond Haiku system libraries,
  OpenSSL, and libfuse

## Quick start

### The WON app

For quick iteration you can build and run the GUI app directly:

```
make apps
./tests/bricola/campiello_won
```

The window lists every service found on the LAN, grouped by kind. Select a device and:

- **Apri** - open the share, or launch the right helper for the service
- **Web UI** - open the device's web interface in a browser
- **Ispeziona** - SSH into a Terminal, start an RDP session, or Wake-on-LAN
- **Dettagli** - expand the network panel (MAC, vendor, NetBIOS, SSDP model)
- **Copia IP** - copy the address to the clipboard

The **Opzioni** menu toggles whether the window opens at login; the Deskbar entry and
the resident daemon are always available regardless.

### The two modes

**Interop** lets Haiku mount the rest of the world. The `campiello_mount` helper and the
`campiello_sftp` userlandfs add-on mount a remote SFTP host as a disk in Tracker - read-only by
default, or read-write when you tick "Consenti la scrittura" in the connect window; the optional
SMB package does the same for Windows shares.

**Native** is Campiello talking to another Campiello. The resident `campiello_daemon`
advertises `_campiello._tcp`, serves your shared folder over mutual-TLS CNP, and the
`campiello_net` add-on shows discovered peers as a `/Campiello` volume. The first time
two machines meet, each shows a single "allow this computer" prompt; after that they
just work.

### Distributed live query

Open a query on a peer and its matches appear under `/Campiello`'s virtual
`/.query/<predicate>` folder. The results are not a one-off snapshot: the server runs a
live `BQuery` and pushes updates over the connection, so files created or removed on the
peer appear and disappear in your folder in real time. This is BFS-native and has no
equivalent in SMB or NFS.

### Radar

Radar is a standalone mDNS/DNS-SD browser for debugging discovery: it decodes the
service types, hosts, ports, and TXT records visible on the network.

```
make apps
./tests/bricola/campiello_radar
```

## Device add-ons

An optional suite turns a discovered device into a real action instead of a bare list
entry: Philips Hue lighting, Chromecast (with live screen mirroring and system audio over
Cast Streaming), IPP/eSCL printers and scanners, DAAP, VNC, AirPlay, and more. Each add-on
is matched to its service type, is dependency-free, and ships as a separate package so the
core stays lean. See `docs/ADDONS_SUITE.md`.

## Dispositivi supportati

Campiello riconosce i dispositivi sulla LAN via mDNS/DNS-SD e, quando è installato
l'add-on corrispondente, offre un'azione reale su un doppio clic nella cartella `~/WON`.
Alcuni add-on implementano un vero controllo del dispositivo, altri sono "solo
informazioni" (mostrano lo stato e le istruzioni, con il percorso di controllo pesante
documentato come sviluppo futuro, mai finto). Ogni riga indica il tipo di servizio mDNS
e cosa fa Campiello.

Livelli: **Controllo** = azione reale sul dispositivo; **Info** = solo informazioni e
istruzioni; **Hand-off** = apre un client/viewer installato.

### Multimedia e casting

| Dispositivo | Servizio mDNS | Cosa fa | Livello |
|-------------|---------------|---------|---------|
| Chromecast / Google Cast | `_googlecast._tcp` | stato e volume, casting di un URL/file nel Default Media Receiver, avvio/stop app via DIAL, mirroring dello schermo con audio di sistema | Controllo |
| Amazon Fire TV / Android TV | `_amzn-wplay._tcp`, `_androidtvremote2._tcp` | telecomando del dispositivo | Controllo |
| AirPlay | `_airplay._tcp`, `_raop._tcp` | info del ricevitore e capacità (video, audio, mirroring); streaming come sviluppo futuro | Info |
| DAAP (libreria iTunes) | `_daap._tcp` | login e elenco dei brani della libreria condivisa | Controllo |
| Spotify Connect | `_spotify-connect._tcp` | info dello speaker (nome, marca, modello); controllo dall'app Spotify | Info |
| Amazon Alexa | `_amzn-alexa._tcp` | presenza del dispositivo; nessuna API locale aperta, controllo via app/cloud | Info |

### Stampa e scansione

| Dispositivo | Servizio mDNS | Cosa fa | Livello |
|-------------|---------------|---------|---------|
| Stampante IPP | `_ipp._tcp` | riepilogo stampante e attributi, stampa di un file | Controllo |
| Scanner eSCL / AirScan | `_uscan._tcp` | capacità, scelta di colore/risoluzione/formato, scansione su file | Controllo |

### Condivisione file

| Dispositivo | Servizio mDNS | Cosa fa | Livello |
|-------------|---------------|---------|---------|
| Windows / SMB (CIFS) | condivisioni SMB | monta le condivisioni Windows come disco in Tracker (add-on opzionale) | Controllo |
| FTP | `_ftp._tcp` | navigazione delle cartelle e download di file | Controllo |
| WebDAV | `_webdav._tcp` | navigazione delle cartelle e download di file | Controllo |
| NFS | `_nfs._tcp` | elenco degli export (showmount via ONC RPC) e istruzioni di mount | Controllo |
| AFP | `_afpovertcp._tcp` | info del server (protocollo legacy, si consiglia SMB) | Info |

### Casa e IoT

| Dispositivo | Servizio mDNS | Cosa fa | Livello |
|-------------|---------------|---------|---------|
| Philips Hue | `_hue._tcp` | pairing del bridge, elenco luci con on/off e slider di luminosità | Controllo |
| Daikin (climatizzatore) | `_dkapi._tcp` | accensione, modalità, temperatura target (passi 0,5 °C) e ventola | Controllo |
| ESPHome | `_esphomelib._tcp` | info del dispositivo (versione, progetto, scheda, MAC) e apertura della web UI | Info |
| HomeKit (HAP) | `_hap._tcp` | info dell'accessorio (nome, categoria, stato di pairing); controllo via Apple Home | Info |
| Matter | `_matter._tcp`, `_matterc._udp` | info del dispositivo (vendor/prodotto, stato di commissioning); pairing via app/hub Matter | Info |
| Lutron Caséta | `_sleap._tcp` | presenza del bridge ed endpoint LEAP; controllo richiede il pairing con certificato | Info |

### Rete e infrastruttura

| Dispositivo | Servizio mDNS | Cosa fa | Livello |
|-------------|---------------|---------|---------|
| UPS / NUT | `_nut._tcp` | monitor di sola lettura (stato, batteria, autonomia, carico) | Info |
| eero (router mesh) | `_eero._tcp` | presenza e base_mac; nessuna API locale aperta | Info |

### Accesso remoto

| Dispositivo | Servizio mDNS | Cosa fa | Livello |
|-------------|---------------|---------|---------|
| VNC / desktop remoto | `_rfb._tcp` | costruisce `vnc://host:port` e apre un viewer installato | Hand-off |
| SSH in Terminal | (azione core) | apre una sessione SSH nel Terminal | Controllo |
| RDP | (azione core) | avvia una sessione di desktop remoto | Controllo |

Oltre agli add-on, il core WON offre azioni integrate su ogni dispositivo: apertura
delle condivisioni SMB/CIFS, SSH in un Terminal, desktop remoto RDP, Wake-on-LAN, e
l'arricchimento LAN (produttore/MAC via ARP/OUI, nome NetBIOS, modello SSDP/UPnP).

## Install (package)

```
make packages
pkgman install ./packaging/campiello-0.3.33-1-x86_64.hpkg
pkgman install ./packaging/smb/campiello_smb-0.2.0-15-x86_64.hpkg     # optional, Windows shares
pkgman install ./packaging/cast/campiello_cast-0.6.0-2-x86_64.hpkg    # optional, Google Cast + mirroring
```

The core package installs the WON app, the resident `campiello_daemon` (auto-starts at
login via a user launch job), the `campiello_net` `/Campiello` volume add-on, the SFTP
interop helpers, and the Deskbar replicant.

The optional SMB package needs a fixed `libsmb2` (the stock HaikuPorts build has a Haiku
errno bug in its connect path; the fix is in `docs/SMB.md`). Mounting a userlandfs
volume should be exercised in a throwaway VM first (an unmount hazard, see
`docs/VERIFIED.md`).

## Build

Requires Haiku with GCC, OpenSSL (`openssl_devel`), the userlandfs FUSE headers,
libssh2, and standard system libraries (`libbe`, `libnetwork`, `libmedia`).

```
make            # same as `make check`
make check      # build and run every unit-test suite
make packages   # build the core and optional .hpkg packages
make all        # check + packages
make apps       # build the Haiku GUI apps (radar, WON) for quick dev
make clean      # clean every subdirectory
```

Each subdirectory keeps its own Makefile; the root one drives them. The portable suites
run anywhere (Linux CI too); the Haiku GUI and FUSE pieces build on Haiku.

## Architecture

Four replaceable layers (Venetian working names):

- **Fondamenta** - the userlandfs add-on that presents peers as mountable volumes in
  Tracker (libfuse 2.x high-level API)
- **Traghetto** - the native transport and protocol (CNP), TLS 1.3 over TCP
- **Bricola** - the discovery daemon, advertising and browsing `_campiello._tcp` over
  mDNS/DNS-SD, feeding the Deskbar replicant
- **WON / Vicinato** (`src/vicinato/`) - the network-neighborhood app, built on Bricola
  discovery and the Fondamenta backends

See `docs/PROPOSAL.md` for the full picture.

## Documentation

- `CONTRIBUTING.md` - conventions and guardrails for working on the project
- `docs/PROPOSAL.md` - the full design document and driving context
- `docs/PROTOCOL.md` - the Campiello Native Protocol (CNP) wire spec, kept authoritative
- `docs/VERIFIED.md` - facts checked against the Haiku source, kept authoritative
- `docs/NEIGHBORHOOD.md` - the WON network-neighborhood app and its actions
- `docs/NETINTEL.md` - the LAN enrichment module (vendor/MAC, NetBIOS, SSDP, Wake-on-LAN)
- `docs/ICONS.md` - the per-service device icon set and its sources
- `docs/ADDONS_SUITE.md` - the optional device add-on suite
- `docs/LOCALIZATION.md` - the Haiku Locale Kit wiring, catalogs, and refresh workflow
- `docs/REUSE.md` - what was harvested from existing Haiku projects and what is greenfield

## License

MIT - see [LICENSE](LICENSE). Copyright © 2026 atomozero. Core code, and anything
statically linked into it, must be permissive (MIT, BSD, Apache-2.0, ISC, zlib, public
domain); GPL/LGPL code lives only under `optional/`, dynamically loaded and off in the
default build.

## Be careful
> **Developer's Note**: This software may contain traces of peanuts and LLM. It has been
> built with a lot of affection for the Haiku platform.

## Support

If you find this project useful, you can buy me a coffee: [![Buy Me A Coffee](https://img.shields.io/badge/Buy%20Me%20A%20Coffee-atomozero-yellow?logo=buymeacoffee)](https://buymeacoffee.com/atomozero)
