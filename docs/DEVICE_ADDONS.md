# Campiello device add-ons (design proposal, DRAFT)

Status: proposal, not yet implemented. Seeking direction before writing code.

## The idea

Campiello already discovers the LAN and classifies each service (`ServiceKind`: Campiello, Smb,
Sftp, Home, Web, Printer, Media, Other) and drops a per-device shortcut in `~/WON`. Today the
shortcut's action is fixed by kind: an SMB share opens the mount helper, a web service opens the
browser, everything else is an info card.

Many devices offer far more than "browse files": a Philips Hue bridge controls lights, a Fire TV
takes a remote / casts, an IPP printer prints. A **device add-on** is an optional, separately
installed module that adds real interaction for one class of device, without bloating the MIT core.

## Principles (from the working agreement)

- **MIT core stays clean.** Each add-on is its own hpkg. Any vendor SDK or LGPL/GPL dependency lives
  in that add-on's package (dynamically loaded), never in core. Same rule that keeps libsmb2 out of
  the core today.
- **One hpkg install, zero-config first use, security always on / one-tap allow.** A newly installed
  add-on should "just light up" for the devices it handles.
- **Treat devices as untrusted.** Validate every response; no unchecked writes; bound every buffer.
- **Reuse what exists.** mDNS discovery (`MdnsRadar`/`NetworkDirectory`), the proven "launch a helper
  app by signature with prefill arguments" pattern (the SMB helper), the WON folder/list, and the
  encrypted secret store (AES-256-GCM keyfile) we built for SMB passwords.

## Architecture

### 1. The handler contract

A device add-on is a Haiku **application** (not an in-process plugin) with:

- an `app_signature`, e.g. `application/x-vnd.Campiello-hue`;
- a **manifest** declaring which devices it handles and what it can do;
- a **launch protocol**: Campiello launches it with the device passed as `key=value` arguments,
  exactly like the SMB helper takes `server=...`. The add-on then shows its control window or
  performs a headless action.

Separate apps (rather than shared libraries loaded into the WON process) are chosen for **crash
isolation** (a flaky vendor protocol can't take down WON) and **licensing isolation** (a GPL SDK
stays in its own binary). It is also the pattern already working for SMB/SFTP.

### 2. Manifest and registration

Each add-on installs a small manifest into a well-known directory, e.g.
`/system/data/campiello/handlers/<name>.handler` (system) or
`~/config/settings/Campiello/handlers/` (user). A manifest declares:

```
signature = application/x-vnd.Campiello-hue
name      = Philips Hue
# Match rules (any match wins): by mDNS service type, by kind, by a TXT/vendor predicate.
match.type = _hue._tcp
match.type = _hap._tcp
# Actions offered in menus; "open" is the default double-click action.
action.open   = Controlla le luci
action.toggle = Accendi/spegni
```

Campiello scans this directory into a `HandlerRegistry`. (Alternative considered: pure MIME
preferred-app, like the SMB `.share` type. Rejected as the primary mechanism because one device can
have several handlers and match rules richer than one type; the MIME/preferred-app trick is still
used for the actual launch.)

### 3. Matching

`HandlerRegistry` maps each discovered `NetworkService` to zero or more handlers whose match rules
apply. A service may match several (a TV that is both castable and remote-controllable). Matching
lives next to `NetworkDirectory` (the existing classifier), so it is one place, unit-testable
off Haiku (pure data).

### 4. Invocation / UI integration

- **WON folder**: a device's shortcut, on double-click, launches its **default** handler (`open`),
  passing the device. With no handler it keeps today's behavior (mount / bookmark / info card).
- **WON app list**: right-click a device -> a menu of its handlers' actions (each launches the
  handler with `action=<id>`).
- The handler receives the device and either acts headlessly (`toggle`) or shows a control window
  (a Hue color panel, a Fire TV remote).

### 5. The device, as passed to a handler

A stable, percent-encoded argument set (same encoding as the SMB mount params):
`host=`, `port=`, `type=` (mDNS service type), `name=`, `action=` (optional), and each TXT record as
`txt.<key>=<value>`. The add-on parses only what it needs.

### 6. Credentials / pairing

Devices needing pairing (Hue bridge button press, Android TV PIN) store their token in the existing
encrypted secret store (`smb_secret.key` + a per-add-on secrets file), keyed by host. One-tap pairing
UI, secret never shown. Reuse `SavePassword`/`LoadPassword`-style helpers, generalized.

## Candidate add-ons (each a separate package)

| Add-on | mDNS type(s) | What it does | Difficulty |
|--------|--------------|--------------|------------|
| **campiello_hue** | `_hue._tcp` | pair, list lights/rooms, on/off, brightness, colour, scenes via the local Hue REST API (HTTP/JSON, no vendor SDK) | **low** - great first target |
| **campiello_cast** | `_googlecast._tcp`, `_airplay._tcp` | cast a media file/URL to a TV | medium |
| **campiello_firetv** | `_amzn-wplay._tcp`, `_androidtvremote2._tcp`, `_dial._tcp` | launch an app / cast (DIAL, HTTP) first; then a full remote (Android TV Remote v2 = TLS + protobuf + PIN pairing) | DIAL low, remote high |
| **campiello_print** | `_ipp._tcp` | print a file over IPP | medium |
| **campiello_home** | `_hap._tcp`, `_matter._tcp` | generic HomeKit/Matter control (pairing + crypto) | high |

## Phasing

1. **Framework only.** Manifest dir + `HandlerRegistry` + WON folder/list integration + the launch
   argument protocol, plus one trivial example handler (e.g. "open the device's web UI") to prove the
   path end to end. No real device protocol yet. Small, reviewable commits.
2. **First real add-on: Philips Hue.** Local HTTP/JSON, no heavy crypto, immediately visible result
   (a light turns on). Validates pairing + the secret store + a control window.
3. **Fire TV (DIAL launch, then remote), casting, printing** as separate follow-ups.

## Open questions (need your call)

1. **Manifest**: a `handlers/` directory (flexible, recommended) vs pure MIME preferred-app (simpler)?
2. **First add-on**: Philips Hue (easiest + visible)? Or Fire TV first because you use it?
3. **Handler form factor**: separate apps (recommended, crash/licensing isolation) vs in-process
   plugins?
4. **Scope now**: land this design, then build **Phase 1 (framework)**? Or prototype one add-on
   (e.g. Hue) directly to feel the shape first?

## Status (2026-07)

Decisions taken and shipped:

- **Manifest**: a `handlers/` directory (Q1). Manifests are `*.handler` files under
  `/system/data/campiello/handlers/` (system add-ons) and `<settings>/Campiello/handlers/` (user);
  `HandlerRegistry::LoadFromDir` reads both. `ShareFolder::LoadHandlers` loads them on every sync.
- **Handler form factor**: separate apps launched by signature (Q3), for crash/licensing isolation.
  A device add-on links whatever it needs (OpenSSL, a vendor protocol) without touching the MIT core.
- **First real add-on**: **Fire TV** (Q2), because the user has one. `campiello_firetv` is a working
  on-screen remote over the Fire TV official REST API (no ADB), shipped as its own optional package
  (`packaging/firetv`, requires libssl/libcrypto). PIN pairing, token in the settings store.

WON integration (live, end-to-end):

1. The WON app matches a discovered service against the registry. A match writes a **device
   shortcut** whose per-file preferred app (`BEOS:PREF_APP`) is the handler signature, carrying
   `CAMPIELLO:host`, `CAMPIELLO:name`, `CAMPIELLO:type` attributes. SMB keeps its mount helper; a
   handler match wins over the generic info card.
2. Double-clicking the shortcut launches the add-on, which reads those attributes in `RefsReceived`
   and opens on the right device.

Still open: generalize the encrypted secret store for add-ons (Fire TV currently keeps its token in a
plain `firetv_tokens` file, 0600); Philips Hue as the second device; DIAL/casting.
