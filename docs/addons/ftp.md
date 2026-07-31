# campiello_ftp — FTP add-on

A Campiello device add-on (see `docs/DEVICE_ADDONS.md`) that browses and downloads from an FTP server.
Seventh component of the suite in `docs/ADDONS_SUITE.md`.

## What it is

FTP servers advertise themselves over mDNS as `_ftp._tcp`. Users want to browse the shared files and
pull one down without a separate client.

## How it works (protocol)

FTP (RFC 959) uses a text control connection on port 21 and separate data connections:

- Read the `220` greeting, then log in: `USER <name>` (`331` needs a password) then `PASS <pass>`
  (`230` on success). Anonymous access is `USER anonymous` / `PASS anonymous@`.
- `TYPE I` selects binary transfers.
- `PASV` asks the server for a data port; the `227` reply carries `(h1,h2,h3,h4,p1,p2)` where the data
  endpoint is `h1.h2.h3.h4:(p1*256+p2)`. The client opens that socket for the next transfer.
- `CWD <dir>` changes directory; `LIST` streams a directory listing over the data connection (Unix
  `ls -l` format on most servers); `RETR <file>` streams a file.

## Integration into Campiello

- `optional/ftp/FtpClient.{h,cpp}`: hand-rolled FTP over plain sockets. `Connect` (login + binary),
  `List` (CWD + PASV + LIST), `Retrieve` (PASV + RETR to a local file). `ReadReply` handles
  multi-line replies; `ParseListing` turns the `ls -l` output into `(name, isDir, size)` entries
  (unit-tested, including names with spaces and symlinks). No third-party library, so **MIT-clean**.
- `optional/ftp/campiello_ftp.cpp`: the browser app. User/password fields (anonymous by default), a
  path label, and a `BListView` of entries (folders first). Double-click a folder to navigate (`..`
  goes up), double-click a file to download it via a `BFilePanel` save panel. Each operation runs on
  its own worker thread with its own short-lived connection, so the UI never blocks. `RefsReceived`
  reads `CAMPIELLO:host/name` from the WON device shortcut.
- `optional/ftp/ftp.handler`: matches `_ftp._tcp`. `packaging/ftp` builds `campiello_ftp-0.1.0-1`
  (needs only `libbe`/`libnetwork`/`libtracker`).

## Licensing

No third-party code or library. FTP is implemented from RFC 959. Fits the MIT core rule (kept under
`optional/` only because it is device-specific).

## Reference material

- RFC 959 (File Transfer Protocol) for the command/reply grammar, PASV, and the transfer commands.

## Testing status

The `ParseListing` parser is unit-tested (a sample `ls -l` listing decodes to the right folders/files,
sizes, a name with spaces, and a symlink). The control/data flow follows RFC 959. **Not yet validated
against a live FTP server** (none in the dev environment). To validate: run
`campiello_ftp host=<server-ip>`, browse, and download a file.

## Follow-ups

- FTPS (FTP over TLS, `AUTH TLS`): would add OpenSSL (Apache-2.0), like campiello_firetv/hue.
- Upload (STOR), rename, delete; MLSD machine-readable listings for robust parsing.
- Mount the server as a volume via the SFTP/SMB-style userlandfs path (bigger, shared with the file
  backends).
