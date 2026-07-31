# Il Vicinato: a WON-style network neighborhood on the Desktop

Status: **Option A shipped; Option B designed, awaiting a build.** This is the unifying vision for Campiello, the
modern take on BeOS "World-O-Networking": a folder on the Desktop where you see an icon for every
service found on the network, and a double-click enters it (a virtual folder of its files) or logs
you in first if it is protected. It ties together everything already built: the mDNS discovery
engine (MdnsRadar), the service decoding (RadarLabels), and the SMB / SFTP / CNP backends behind
the common PeerBackend interface.

## What the user wants
- A Desktop folder (call it **Vicinato** or **Rete**).
- Opening it shows an icon per discovered network service: Windows shares, Mac/Samba shares,
  Campiello peers, printers, media devices, web services...
- Double-click a browsable, open service: enter its virtual folder and see its files/data.
- Double-click a protected service: log in first (password for SMB/SFTP, one-tap pairing for a
  Campiello peer), then enter.

## What we already have (reuse, do not rebuild)
- **Discovery**: `MdnsRadar` already finds every mDNS/DNS-SD service on the LAN and, with
  `RadarLabels`, names it (Philips Hue, Matter, SMB, printer...). Campiello peers come from
  Bricola. Windows SMB is the gap (see below).
- **Backends**: `SmbBackend` (working, after the libsmb2 fix), `SftpBackend`, `CnpBackend`, all
  behind `PeerBackend`. `PathRouter` already routes `/<service>/<path>` to the right backend.
- **The FUSE front end** (`CampielloFuse`) presents a PeerBackend as a Tracker-browsable volume.
- **Connect helpers** (`campiello_smb_mount`, `campiello_mount`) that log in and mount one service.

## The two gaps
1. **Windows SMB is invisible to mDNS.** Windows announces over NetBIOS (UDP 137) and WS-Discovery
   (UDP 3702 multicast), not Bonjour. A discovery source must add these (or, as a stopgap, the
   TCP-445 subnet probe we already wrote) so Windows PCs appear. Mac/Samba shares that DO advertise
   `_smb._tcp` already show via mDNS.
2. **Login-on-demand under FUSE.** FUSE getattr/readdir run in the mount process, which cannot pop
   a modal login. So "double-click to log in" needs an external trigger (see the options below).

## The design: a unified NetworkDirectory feeding a neighborhood FS
```
 discovery sources ─┐
   MdnsRadar (mDNS) ─┤
   Bricola (peers)  ─┼─> NetworkDirectory ── one NetworkService per entry:
   SMB finder       ─┘      { name, kind, host, port, txt,
   (WS-Disc/445)             browsable?, authKind(none|password|pairing),
                             backend(smb|sftp|cnp|none) }
                                     │
                                     ▼
   CampielloFuse volume mounted at /Vicinato (top-level, shows on Desktop)
     root readdir  -> one dir per service
     /Vicinato/<svc>/...  -> routed by PathRouter to the service's backend
     non-file services (printer, web, cast) -> an info entry, not a folder
```
`NetworkService` is the new abstraction; the rest exists. Mapping service type -> backend:
`_smb._tcp`/Windows -> SmbBackend, `_sftp-ssh`/`_ssh` -> SftpBackend, `_campiello` -> CnpBackend,
everything else -> a read-only info card built from the decoded TXT (RadarLabels).

## The login question (the one real design fork)
Three ways to handle a protected service; pick one to build first.

**A. Companion app (simplest, safest).** A "Vicinato" window (like the radar) shows the service
icons; double-click mounts THAT service as its own top-level disk via the existing connect helper
(prompting for login as needed). Not a single folder, but no FUSE-prompt problem and no unmount
hazard beyond what the helpers already do. Fastest to ship; closest to today's code.

**B. True neighborhood FUSE volume (authentic WON).** /Vicinato is one mounted folder listing all
services. Services you already have credentials for are browsable; a protected one shows a single
`Accedi.campiello-login` file whose double-click launches the connect helper prefilled for that
host (Tracker opens a file with its associated app). Once you log in, the folder becomes live.
Credentials are kept in the existing settings store. This is the real vision, more work, and it
mounts a userlandfs volume (KDL-on-unmount hazard, test in a VM only).

**C. Hybrid.** A background daemon writes a folder of live shortcut files (one per service) into a
real directory on the Desktop; each shortcut opens/mounts on double-click. No custom FS, no
unmount hazard, but weaker "enter the folder and see files" feel.

## Constraints to respect (from the working agreement)
- **Unmount is a KDL hazard.** Any FUSE-volume path (B) must be tested in a throwaway VM, never on
  the main machine. Option A/C avoid a new volume.
- **Experience first**: one install, zero config, security always on but only ever a one-tap allow.
  Passwords for SMB/SFTP are unavoidable (that is the peer's auth), but a Campiello peer must stay
  one-tap pairing.
- **Same-host multicast**: Campiello-peer discovery still needs a second machine; other services
  (real LAN devices) already appear, as the radar proved.

## Progress (2026-07-19)
**Option A is built, packaged, and shipping** (campiello 0.3.0): `campiello_vicinato` lists every
discovered service with coloured per-kind badges and a lock glyph, and a double-click opens the
SMB/SFTP login helper prefilled with the host, or an info card with decoded TXT for non-file
services. The supporting layer is done and unit-tested: `MdnsRadar` + `RadarLabels` (discovery and
naming, src/bricola/mdns), `NetworkDirectory` (classification, src/vicinato), and `SmbHostFinder`
(the TCP-445 sweep that surfaces Windows PCs mDNS cannot see, src/vicinato). The connect helpers
carry app_signature resources so the roster can launch them.

**Option C (the live ~/WON shortcut folder) is now built** (campiello 0.3.4): when the app's
service set changes, `SyncShareFolder` writes one shortcut per discovered service into `~/WON`, each
with a per-kind icon matching the list badges (`ShareFolder.cpp` mirrors `KindColor`/`KindGlyph`).
The double-click action fits the kind: an SMB service is a `.share` (opens the mount helper), a Web
service is a Haiku bookmark (`application/x-vnd.Be-bookmark` + `META:url`, opens the browser), and
any other device (printer, media, home, peer) is a `text/plain` info card. Every shortcut is tagged
with a `CAMPIELLO:won` attribute so pruning only ever removes shortcuts we created, never the user's
files or the live mount directories (SMB shares mount at `~/WON/<server>/`). So the WON folder
doubles as the network neighborhood AND the place the live mounts appear.

**Option B (the true FUSE folder) is the remaining big piece.** It is build-only work here (the
KDL unmount hazard forbids mounting on the main machine; live testing needs a throwaway VM), and
it needs one product decision from the user: the login-under-FUSE flow. Recommended plan when
picked up:
1. A `NeighborhoodSource` (a PeerSource, like ConnectionManager) that lists all `NetworkService`s
   and lazily builds the right backend per service. Reuse `PathRouter` for `/<service>/<rest>`
   routing and `CampielloFuseMain` for the volume, exactly as campiello_net_main does for peers.
2. A credential store under `<settings>/Campiello/` keyed by host/share, so a service you have
   logged into once is browsable transparently on the next mount (SMB/SFTP passwords; Campiello
   peers stay one-tap pairing).
3. **Recommended login flow (fork resolved):** a protected service with no stored credential shows
   a single `Accedi.campiello-login` entry; opening it in Tracker launches the connect helper
   prefilled (Tracker opens a file with its associated app, and the helpers already take
   `server=`/`host=` prefill args). On success the helper writes the credential to the store and
   the folder becomes live on the next refresh. This keeps the "one-tap allow" experience without
   a modal prompt inside the mount process.
4. Non-file services render as a read-only info file (reuse `RadarLabels::BuildRadarReport`-style
   text), not a folder.

## Suggested staging (once a form factor is chosen)
1. `NetworkService` + `NetworkDirectory` aggregating MdnsRadar + Bricola (+ a stub SMB finder). DONE.
2. The chosen surface (A: window DONE; B: FUSE volume; C: shortcut daemon).
3. SMB finder: WS-Discovery / NetBIOS so Windows PCs appear (start with the 445 probe).
4. Info cards for non-file services (reuse RadarLabels); launch actions (open web, etc.).
5. Icons per service kind.

## Shipped WON app actions (surface A, campiello_won)

The window surface has grown into a full network-neighborhood browser. Beyond discovery and the
login/mount flow above, it offers per-device actions (toolbar + right-click context menu). The
guiding rule: every action does one real thing with one click; pure info-only cards are avoided.

- **Apri / Web UI / Copia IP / Ispeziona** the selected service.
- **Apri terminale SSH** (`OpenSshTerminal`): opens Haiku Terminal running `ssh <host>` for
  computers and login hosts (SSH/SFTP, Campiello peers, Windows/SMB, or anything advertising
  `_ssh`/`_sftp-ssh`). Terminal is B_MULTIPLE_LAUNCH, so each call gets its own window; a username
  hint from the service TXT (u/user/username) becomes user@host.
- **Apri desktop remoto (RDP)** (`OpenRdp`): launches an installed RDP client (the FreeRDP family
  with `/v:host`, or remmina with `-c rdp://host`) for Windows shares, computers, and `_rdp` hosts;
  a friendly note points to freerdp/remmina on HaikuDepot when none is installed.
- **Accendi (Wake-on-LAN)**: sends a magic packet to any device whose MAC we have learned (see
  NetIntel); learned MACs are persisted so a sleeping device can still be woken.

## Computers as first-class devices

`_workstation._tcp`, `_device-info._tcp` and `_companion-link._tcp` classify as `ServiceKind::Computer`
(RadarLabels already names them), so plain machines that advertise no shareable service appear in a
"Computer" group with a computer icon; double-clicking one opens an SSH terminal. `BuildNeighborhood`
dedups: a machine that also offers a real service (share, SSH, web) is shown once by that service, so
the Computer group only surfaces hosts nothing else covers.

## Live status via TCP + ARP

Reachability is checked by a TCP connect to the service port (giving latency in ms). A closed port
does not mean the host is down: when the TCP check fails but the address is in the ARP table (learned
by the NetIntel pass), the device is shown online, "raggiungibile in rete" (green "in rete" pill, no
latency). This fixes false "offline" for hosts whose admin ports are filtered, such as the home
router whose wifi is in use. See docs/NETINTEL.md for the enrichment (vendor/MAC, NetBIOS names,
SSDP/UPnP discovery, Wake-on-LAN).

## Details panel

The details pane is a custom zebra-striped table (`DetailTable`): a wrapping device-name title, a
coloured status line, and label/value rows in alternating white/grey. Minor rows live in collapsible
groups the user folds by clicking the header ("Dettagli di rete" for MAC and raw service type,
"Informazioni sul servizio" for the decoded mDNS TXT, collapsed by default when long).
