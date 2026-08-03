# campiello_daap - DAAP music-library add-on

A Campiello device add-on (see `docs/DEVICE_ADDONS.md`) that browses a shared iTunes / OwnTone music
library over DAAP. Fifth component of the suite in `docs/ADDONS_SUITE.md`.

## What it is

iTunes and servers like OwnTone (forked-daapd) share a music library on the LAN, advertised over
mDNS as `_daap._tcp`. Users want to see what is in the library from their computer.

## How it works (protocol)

DAAP is HTTP on port 3689 whose responses are DMAP: a binary type-length-value tree. Each element is
a 4-byte ASCII content code, a 4-byte big-endian length, then the value; container elements nest
child elements.

- `GET /login` returns `mlog { mlid = session-id }`.
- `GET /databases?session-id=N` returns `avdb { ... mlit { miid = database id } }`.
- `GET /databases/<id>/items?session-id=N&meta=dmap.itemid,dmap.itemname,daap.songartist,daap.songalbum&type=music`
  returns `adbs { ... mlcl { mlit { minm=title, asar=artist, asal=album } ... } }`.

Content codes used: `mlid` (session-id), `miid` (item/database id), `minm` (item name = track
title), `asar` (song artist), `asal` (song album).

## Integration into Campiello

- `optional/daap/DaapClient.{h,cpp}`: hand-rolled DAAP over plain HTTP with a small DMAP parser. No
  third-party library, so **MIT-clean with no extra dependency**. `dmap::FindLeaf` (recursive TLV
  search), `dmap::ParseTracks` (one Track per `mlit`), and `dmap::AsInt`; a container heuristic walks
  the tree without a full content-code table. Unit-tested.
- `optional/daap/campiello_daap.cpp`: the browser app. Logs in, finds the first database, and lists
  its tracks in a `BListView` (artist, title, album), on a worker thread. `RefsReceived` reads
  `CAMPIELLO:host/name` from the WON device shortcut.
- `optional/daap/daap.handler`: matches `_daap._tcp`. `packaging/daap` builds `campiello_daap-0.1.0-1`
  (needs only `libbe`/`libnetwork`).

## Licensing

No third-party code or library. DAAP/DMAP is implemented from public protocol documentation. Fits the
MIT core rule (kept under `optional/` only because it is device-specific).

## Reference material

- `github.com/bjoernricks/daap-protocol` - DAAP protocol documentation (endpoints, login/session
  flow, meta fields).
- `github.com/mattstevens/dmap-parser` - the DMAP TLV layout and the `minm`/`asar`/`asal`/`miid`/
  `mlid` content codes.

## Testing status

The DMAP parser is unit-tested (a synthetic login response yields the session-id; a synthetic items
response decodes to two tracks with the right artist/title/album). **Not yet validated against a live
DAAP server** (none in the dev environment). To validate: run `campiello_daap host=<library-ip>` and
check the track list. Note: iTunes libraries may require pairing/authentication; OwnTone is open by
default.

## Follow-ups

- Stream/play a track: `GET /databases/<id>/items/<trackid>.<fmt>?session-id=N` returns the audio;
  hand it to the media kit (or download). Adds a player UI.
- Playlists (`/databases/<id>/containers`), search, and cover art.
- Password-protected libraries (Basic auth with the library password).
