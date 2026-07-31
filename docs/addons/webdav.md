# campiello_webdav — WebDAV add-on

A Campiello device add-on (see `docs/DEVICE_ADDONS.md`) that browses and downloads from a WebDAV
server. Eighth component of the suite in `docs/ADDONS_SUITE.md`.

## What it is

WebDAV (Web Distributed Authoring and Versioning) turns an HTTP server into a file share; Nextcloud,
Apache `mod_dav`, and many NAS boxes speak it, advertised over mDNS as `_webdav._tcp`. Users want to
browse and pull files.

## How it works (protocol)

WebDAV (RFC 4918) extends HTTP:

- `PROPFIND <path>` with header `Depth: 1` and a small XML body requesting `displayname`,
  `getcontentlength`, and `resourcetype` returns a **207 Multistatus** XML: one `<D:response>` per
  member, each with a `<D:href>` (path), a `<D:displayname>`, a `<D:getcontentlength>` (files), and a
  `<D:resourcetype>` that contains `<D:collection/>` for folders. The first response is the collection
  itself (skipped).
- `GET <href>` downloads a resource.
- Optional `Authorization: Basic base64(user:pass)` for protected shares.

## Integration into Campiello

- `optional/webdav/WebDavClient.{h,cpp}`: hand-rolled WebDAV over plain HTTP. `List` (PROPFIND Depth 1
  + parse), `Download` (GET to a local file), and a namespace-prefix-agnostic multistatus parser
  (`xml::Blocks`/`Tag`/`ParseMultistatus`, plus `UrlDecode` and a small Base64 for auth). No
  third-party library, so **MIT-clean**. The parser is unit-tested.
- `optional/webdav/campiello_webdav.cpp`: the browser app. User/password fields (empty = anonymous), a
  path label, and a `BListView` of resources (folders first). Double-click a folder to navigate (`..`
  goes up), double-click a file to download via a `BFilePanel`. Each operation runs on its own worker
  thread. `RefsReceived` reads `CAMPIELLO:host/name` from the WON device shortcut.
- `optional/webdav/webdav.handler`: matches `_webdav._tcp`. `packaging/webdav` builds
  `campiello_webdav-0.1.0-1` (needs only `libbe`/`libnetwork`/`libtracker`).

## Licensing

No third-party code or library. WebDAV is implemented from RFC 4918. Fits the MIT core rule (kept
under `optional/` only because it is device-specific).

## Reference material

- RFC 4918 (HTTP Extensions for Web Distributed Authoring and Versioning) for PROPFIND, the
  multistatus response, and the DAV: property names.

## Testing status

The multistatus parser is unit-tested (a synthetic 207 body decodes to the right folders/files, sizes,
and percent-decoded names/hrefs, skipping the collection's own entry). **Not yet validated against a
live WebDAV server** (none in the dev environment). To validate: run `campiello_webdav host=<server>`,
browse, and download a file.

## Follow-ups

- HTTPS WebDAV (`_webdavs._tcp`): would add OpenSSL (Apache-2.0), like campiello_firetv/hue.
- Upload (PUT), MKCOL, DELETE, MOVE for read-write use.
- Mount the share as a userlandfs volume (bigger, shared with the file backends).
