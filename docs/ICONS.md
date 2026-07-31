# Campiello icons

Campiello can show a real HVIF vector icon per service in the WON list (and on the `~/WON` shortcuts).
Where an icon is missing it falls back to the built-in coloured letter badge, so icons are always
optional.

## Where icons are looked up

For a base name `<name>`, the first file found wins:

1. User: `~/config/settings/Campiello/icons/<name>.hvif`  (per-user, never redistributed)
2. System: `/system/data/campiello/icons/<name>.hvif`      (the base set, shipped in the core package)

Icons are Haiku Vector Icon Format (HVIF). Make them with Icon-O-Matic, or convert SVG/`.iom` with
`hvif-tools` (github.com/threedeyes/hvif-tools).

## The shipped base set (MIT)

The core package installs a full base set (see `optional/icons/`), so every discovered service gets
a real device icon out of the box; drop a file in the user dir above to override any of them. The
device icons are converted from the MIT-licensed `HVIF-Collection`
(github.com/threedeyes/HVIF-Collection, (c) 2019 Gerasim Troeglazov) with the same author's
`iom2hvif`, from the classic McClintock BeOS set; `campiello.hvif` is atomozero's own world/globe.
No brand logos are used (no trademarks). Full source, license and the per-kind mapping are recorded
in `optional/icons/ATTRIBUTION`.

## Icon base names

A service maps to a base name by its DNS-SD type first, then by kind (see `IconBaseName` in
`src/vicinato/campiello_vicinato.cpp`). Provide any subset; missing ones fall back to the badge.

| name | used for |
|------|----------|
| `campiello` | native Campiello peers (`_campiello._tcp`) |
| `smb`      | Windows shares (`_smb._tcp`) |
| `ssh`      | SSH / SFTP (`_ssh._tcp`, `_sftp-ssh._tcp`) |
| `ftp`      | FTP (`_ftp._tcp`) |
| `webdav`   | WebDAV (`_webdav._tcp`, `_webdavs._tcp`) |
| `nfs`      | NFS (`_nfs._tcp`) |
| `afp`      | Apple Filing (`_afpovertcp._tcp`) |
| `hue`      | Philips Hue (`_hue._tcp`) |
| `homekit`  | Apple HomeKit (`_hap._tcp`) |
| `matter`   | Matter (`_matter._tcp`, `_matterc._udp`) |
| `lutron`   | Lutron (`_sleap._tcp`) |
| `airplay`  | AirPlay (`_airplay._tcp`, `_raop._tcp`) |
| `cast`     | Google Cast (`_googlecast._tcp`) |
| `spotify`  | Spotify Connect (`_spotify-connect._tcp`) |
| `firetv`   | Amazon Fire TV (`_amzn-wplay._tcp`) |
| `alexa`    | Amazon Alexa (`_amzn-alexa._tcp`) |
| `daap`     | iTunes/DAAP library (`_daap._tcp`) |
| `printer`  | IPP / LPR printers (`_ipp._tcp`, `_printer._tcp`, ...) |
| `scanner`  | eSCL scanners (`_uscan._tcp`, `_scanner._tcp`) |
| `web`      | web services (`_http._tcp`, `_https._tcp`) |
| `vnc`      | remote desktops (`_rfb._tcp`) |
| `home`     | fallback for Home-kind devices |
| `media`    | fallback for Media-kind devices |
| `other`    | fallback for anything else |

## The `campiello_icons` package

`optional/icons/` holds the shipped icon set and `packaging/icons/` builds `campiello_icons-<v>.hpkg`,
which installs each `optional/icons/*.hvif` to `/system/data/campiello/icons/`. Drop the `.hvif` files
in, name them per the table, run `make package` in `packaging/icons`, and install.

## Licensing / attribution

The bundled icon set is NOT part of the MIT core: it is a separate optional package with its own
attribution. Icons sourced from hvif-store.art are by their respective authors (site by 3dEyes /
threedeyes); include the authors' names and the granted terms in `optional/icons/ATTRIBUTION` before
shipping. Brand logos (Netflix, Prime, YouTube, ...) carry the respective companies' trademarks, which
the icon author cannot grant; prefer generic device icons, or use brand marks only with the mark
owner's terms in mind.
