# Localization (Haiku Locale Kit)

Campiello is localized through the standard Haiku Locale Kit. Every shipped
application resolves its user-facing strings against a catalog at runtime, so the
same binary speaks the user's language when a matching catalog is installed and
falls back to the built-in strings otherwise.

## Source language and translations

The source strings, the literals passed to `B_TRANSLATE(...)` in the code, are
**Italian**. This is deliberate: Italian is both the reference text and the
fallback shown when no catalog matches the user's language (see the guiding note
in [`README.md`](README.md): developer docs in English, end-user strings in
Italian). Each application ships:

- `it.catkeys`: an identity catalog (the translation column equals the source),
  regenerated mechanically from the source.
- `en.catkeys`: the English translation, hand-authored.

The `.catkeys` files are the tracked source of truth (under
`optional/<name>/locales/` for the device add-ons, `src/vicinato/locales/` for the
WON app). The compiled `.catalog` files are build artifacts and are gitignored;
they are produced at package time and installed to
`data/locale/catalogs/<signature-without-application-prefix>/<lang>.catalog`,
where `BLocaleRoster` looks for them.

## What is localized

| Surface | Catalog signature | Context(s) |
| --- | --- | --- |
| WON app (`campiello_won`) | `application/x-vnd.Campiello-won` | `WON` |
| Every device add-on (`campiello_<name>`) | `application/x-vnd.Campiello-<name>` | one per add-on (`Hue`, `Cast`, `IPP`, ...) |

The SMB mount helper uses the signature `application/x-vnd.Campiello-smb-mount`.

Protocol tokens, mDNS service types, `app_signature` strings, argv keys
(`host=`, `CAMPIELLO:...`), `BMessage` codes, internal view names, format
specifiers and stderr/debug logging are intentionally **not** wrapped: they are
not user-facing and translating them would break lookups or logic. A few add-ons
(AirPlay, HomeKit, Matter, Fire TV) keep their label tables as data and mark the
strings with `B_TRANSLATE_MARK`, translating them at the point of display.

## How the build wires it in

Each application links `-llocalestub` so `B_TRANSLATE` resolves the app's catalog
at runtime. The packaging `Makefile` for each component:

1. builds the catalog from its checked-in catkeys:
   `linkcatkeys -l <lang> -s <signature> -o <lang>.catalog locales/<lang>.catkeys`
2. installs it into `data/locale/catalogs/<catalog-dir>/` inside the package.

The WON app follows the same pattern in the core `packaging/Makefile`.

## Refreshing the catkeys after changing strings

After adding or changing a `B_TRANSLATE(...)` string in the source, regenerate the
identity `it.catkeys` and then merge the new keys into the other languages by
hand. From the component's packaging directory:

```
make catkeys
```

That preprocesses the source in collecting mode
(`-DB_COLLECTING_CATKEYS -E -P`) and runs `collectcatkeys` to rewrite
`locales/it.catkeys`. The header fingerprint it writes must be copied verbatim
into every sibling `locales/*.catkeys` (only the language field and the translation
column differ between languages); a fingerprint mismatch makes the catalog fail to
load.

## Adding a new language

1. Copy `it.catkeys` to `locales/<lang>.catkeys`.
2. Change the language field in the header line from `it` to `<lang>` (keep the
   fingerprint).
3. Replace the translation column (the last tab-separated field) of each row with
   the translated text, keeping every `%`-format specifier and `\n` identical to
   the source.
4. Add `<lang>` to `LANGS` in the packaging `Makefile`.
