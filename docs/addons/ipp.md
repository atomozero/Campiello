# campiello_ipp - IPP printing add-on

A Campiello device add-on (see `docs/DEVICE_ADDONS.md`) that talks to a network printer over the
Internet Printing Protocol (IPP), the protocol behind AirPrint and modern CUPS printers. Second
component of the suite in `docs/ADDONS_SUITE.md`.

## What it is

Most network printers advertise themselves over mDNS as `_ipp._tcp` (and `_ipps._tcp` for TLS) and
speak IPP: a request/response protocol carried over HTTP. Users want to see the printer's status and
send a document to it without installing a driver.

## How it works (protocol)

IPP is an HTTP POST to `http://<ip>:631/<resource-path>` with `Content-Type: application/ipp`. The
body is a binary IPP message:

```
version   : 2 bytes  (0x01 0x01 = IPP/1.1)
operation : 2 bytes  (Get-Printer-Attributes 0x000B, Print-Job 0x0002)
request-id: 4 bytes
groups    : operation-attributes-tag 0x01, then attributes, ... , end-of-attributes-tag 0x03
attribute : value-tag(1) | name-len(2) | name | value-len(2) | value
```

- **Get-Printer-Attributes** returns a printer-attributes group with `printer-name`,
  `printer-make-and-model`, `printer-state` (3 idle / 4 processing / 5 stopped),
  `printer-location`, and `document-format-supported`.
- **Print-Job** carries the same operation attributes plus `requesting-user-name`, `job-name`, and
  `document-format`, followed by the raw document bytes. AirPrint printers accept `application/pdf`,
  `image/jpeg`, `image/urf` (Apple raster); most also accept `application/octet-stream` (auto-detect).

The resource path and supported formats come from the printer's mDNS TXT record (`rp`, `pdl`); this
add-on defaults `rp` to `ipp/print` (the AirPrint default).

## Integration into Campiello

- `optional/ipp/IppClient.{h,cpp}`: hand-rolled IPP encode/decode over plain HTTP. No third-party
  library, so this module is **MIT-clean with no extra dependency** (unlike the TLS-based add-ons).
  `wire::` exposes the builders/parsers; the encode/parse round-trip is unit-tested.
- `optional/ipp/campiello_ipp.cpp`: the panel app. Queries the printer on a worker thread and shows a
  summary + the full attribute list; a "Stampa un file..." button opens a `BFilePanel` and submits
  the chosen file (document-format guessed from the extension) on another worker thread. `RefsReceived`
  reads `CAMPIELLO:host/name` from the WON device shortcut.
- `optional/ipp/ipp.handler`: matches `_ipp._tcp`. `packaging/ipp` builds `campiello_ipp-0.1.0-1`
  (needs only `libbe`/`libnetwork`/`libtracker`, all part of Haiku).

## Licensing

No third-party code or library. IPP is implemented from the public PWG specification. Fits the MIT
core rule (kept under `optional/` only because it is device-specific).

## Reference material

- PWG "How to Use the Internet Printing Protocol" (`istopwg.github.io/ipp/ippguide.html`) - the
  operations, attribute tags, and message layout.
- `github.com/williamkapke/ipp` and `github.com/watson/ipp-encoder` (Node.js) - cross-checked the
  binary header (version/operation/request-id) and the attribute encoding.

## Testing status

The IPP encode/parse round-trip is verified by a unit test (synthetic Get-Printer-Attributes response
decodes to the right name/state/formats, including multi-value attributes). **Not yet validated
against a physical printer** (none in the dev environment). To validate: run
`campiello_ipp host=<printer-ip>`, check the shown attributes, then print a PDF.

## Follow-ups

- `_ipps._tcp` (IPP over TLS): would add OpenSSL (Apache-2.0) and move the module to the OpenSSL-linked
  optional set, like campiello_firetv/hue.
- Read `rp`/`pdl` from the discovery TXT instead of defaulting, so non-AirPrint resource paths work.
- Job status polling (Get-Job-Attributes) and cancel.
