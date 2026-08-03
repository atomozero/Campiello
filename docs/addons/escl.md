# campiello_escl - eSCL / AirScan scanning add-on

A Campiello device add-on (see `docs/DEVICE_ADDONS.md`) that scans from a network scanner over eSCL
(Apple AirScan), the driverless scanning protocol behind most modern multifunction printers. Third
component of the suite in `docs/ADDONS_SUITE.md`.

## What it is

Scanners and MFPs advertise themselves over mDNS as `_uscan._tcp` (and `_uscans._tcp` for TLS) and
speak eSCL, an XML-over-HTTP protocol. Users want to scan a page to a file without a driver.

## How it works (protocol)

Base URL `http://<ip>:<port>/eSCL` (port and resource path `rs` come from the mDNS SRV/TXT).

1. **Capabilities**: `GET /eSCL/ScannerCapabilities` returns XML describing the model, color modes
   (`RGB24`, `Grayscale8`, ...), resolutions, and document formats (`image/jpeg`, `application/pdf`).
2. **Start a job**: `POST /eSCL/ScanJobs` with a `ScanSettings` XML body (scan region, input source,
   color mode, resolution, document format). The response is `201 Created` with a `Location` header
   pointing at the job URI (`/eSCL/ScanJobs/<uuid>`).
3. **Fetch pages**: `GET <jobUri>/NextDocument` returns the image bytes with `200 OK`; `202 Accepted`
   means the scan is still in progress (keep polling); `404/410` means there are no more pages.

## Integration into Campiello

- `optional/escl/EsclClient.{h,cpp}`: hand-rolled eSCL over plain HTTP. No third-party library, so
  this module is **MIT-clean with no extra dependency**. `xml::` exposes namespace-prefix-agnostic
  tag readers and the `ScanSettings` builder (unit-tested); `Scan()` posts the job, follows the
  `Location`, and polls `NextDocument`.
- `optional/escl/campiello_escl.cpp`: the panel app. Queries the scanner on a worker thread (shows
  the model and modes/formats), offers color/resolution/format menus, and scans to a file chosen via
  a `BFilePanel` save panel on another worker thread. `RefsReceived` reads `CAMPIELLO:host/name` from
  the WON device shortcut.
- `optional/escl/escl.handler`: matches `_uscan._tcp`. `packaging/escl` builds `campiello_escl-0.1.0-1`
  (needs only `libbe`/`libnetwork`/`libtracker`).

## Licensing

No third-party code or library. eSCL is implemented from the public protocol description. Fits the
MIT core rule (kept under `optional/` only because it is device-specific).

## Reference material

- `github.com/alexpevzner/sane-airscan` - the reference eSCL/WSD SANE backend; used to confirm the
  endpoints, the `201`/`Location` job flow, and the `200`/`202` `NextDocument` polling.
- The "Reverse Engineering eSCL / Apple AirScan" write-up corroborates the `ScanSettings` element
  names and namespaces.

## Testing status

The XML capability parser and the `ScanSettings` builder are unit-tested (a synthetic
`ScannerCapabilities` decodes to the right model, color modes, and formats without matching closing
tags). **Not yet validated against a physical scanner** (none in the dev environment). To validate:
run `campiello_escl host=<scanner-ip>`, check the capabilities, then scan a page.

## Follow-ups

- Read the port and `rs` from the discovery SRV/TXT instead of defaulting to `:80`/`eSCL`.
- `_uscans._tcp` (eSCL over TLS): would add OpenSSL (Apache-2.0), like campiello_firetv/hue.
- Multi-page (ADF) scanning: keep calling `NextDocument` until `404`, writing one file per page.
