# campiello_vnc — VNC remote-desktop add-on

A Campiello device add-on (see `docs/DEVICE_ADDONS.md`) for a VNC / RFB remote desktop discovered on
the network. It hands the connection to an installed VNC viewer rather than implementing the RFB
protocol itself. Sixth component of the suite in `docs/ADDONS_SUITE.md`.

## What it is

Machines running a VNC server advertise themselves over mDNS as `_rfb._tcp` (RFB = Remote FrameBuffer,
the VNC wire protocol). Users want to open the remote screen.

## How it works

- **RFB** (RFC 6143) is the VNC protocol: a TCP connection (default port 5900 + display number),
  a version handshake, a security handshake (often VNC challenge-response), then framebuffer updates
  and input events. Implementing a full client (pixel formats, encodings like Raw/CopyRect/Tight/ZRLE,
  input) is substantial, so this add-on does **not** do it (see follow-up).
- Instead, the add-on builds a `vnc://host:port` URL and asks the system to open it. Haiku registers a
  URL scheme as the MIME type `application/x-vnd.Be-URL.<scheme>`; a viewer that handles `vnc://`
  claims `application/x-vnd.Be-URL.vnc`. The add-on uses `be_roster->FindApp` / `Launch` on that type.
  If no viewer is installed it shows the connection details and points the user to HaikuDepot.

## Integration into Campiello

- `optional/vnc/campiello_vnc.cpp`: the launcher app. `RefsReceived` reads `CAMPIELLO:host/name` from
  the WON device shortcut (port defaults to 5900). It tries `LaunchViewer` immediately; on success it
  says so, otherwise it shows `host:port`, an install hint, an "Apri nel visualizzatore" retry, and a
  "Copia indirizzo" button (clipboard). No network I/O of its own, so no worker thread is needed.
- `optional/vnc/vnc.handler`: matches `_rfb._tcp`. `packaging/vnc` builds `campiello_vnc-0.1.0-1`
  (needs only `libbe`).

## Licensing

No third-party code or library. Fits the MIT core rule (kept under `optional/` only because it is
device-specific).

## Reference material

- RFB protocol specification RFC 6143 (the VNC wire protocol), for the follow-up native client.
- TigerVNC / TightVNC document the common encodings and security types a native client would need.
- Note: Haiku's own `RemoteDesktop` app speaks the Haiku app_server remote protocol, **not** RFB, so
  it is not a VNC viewer.

## Testing status

Compiles and follows the documented launch path. On a stock Haiku there is usually no `vnc://` handler
registered, so the add-on shows the fallback info panel; installing a VNC viewer that claims the
scheme makes the "Apri" path work. Not yet validated end-to-end with a viewer + a live RFB host.

## Follow-ups

- A native RFB client (RFC 6143): version + security handshake, Raw/CopyRect/Tight/ZRLE decoding,
  keyboard/pointer input, drawn into a BView. This is a large module; it would be its own package.
- Read the RFB port from the discovery SRV/TXT instead of defaulting to 5900.
- Offer to install a viewer via the package kit when none is present.
