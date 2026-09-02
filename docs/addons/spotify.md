# campiello_spotify - Spotify Connect add-on

A Campiello device add-on (see `docs/DEVICE_ADDONS.md`) for a Spotify Connect receiver. It shows the
speaker's public info **and** actively controls playback once a Spotify account is linked. Thirteenth
component of the suite in `docs/ADDONS_SUITE.md`.

## What it is

Spotify Connect speakers (Sonos, AVRs, `librespot` devices, smart speakers) advertise themselves over
mDNS as `_spotify-connect._tcp`. From the WON neighborhood you can both inspect a discovered speaker
and drive playback on it from this PC.

## How it works

Two layers, one window:

1. **Info (no account).** The receiver runs a small HTTP server (the "zeroconf" endpoint). Its
   **unauthenticated** `getInfo` action returns public device info as JSON: `remoteName`,
   `brandDisplayName`, `modelDisplayName`, `deviceType`, `version`, `activeUser`. The endpoint path is
   the `CPath` value from the mDNS TXT (often `/`), the port is the SRV port:
   `GET http://<host>:<port><CPath>?action=getInfo`.

2. **Active control (Spotify account).** Playback control goes through the **Spotify Web API** (the
   cloud), not the local zeroconf. Once linked, the add-on can:
   - **Transfer playback** to the discovered speaker (`PUT /v1/me/player`);
   - **Play / Pause / Next / Previous** (`/v1/me/player/{play,pause,next,previous}`);
   - **Set volume** (`/v1/me/player/volume`);
   - show **now playing** (`GET /v1/me/player`).

   The discovered speaker is matched to a Web API device by name (`GET /v1/me/player/devices`), so
   "Trasferisci qui" targets the right `device_id`.

### Why the Web API and not the local `addUser`

The local zeroconf `addUser` action logs a user straight into the device, but its `blob`/`clientKey`
is a Spotify **credentials blob** obtainable only through the full librespot-style authentication
(Shannon cipher + protobuf + Mercury). That is not feasible dependency-free, so it would be a fake
without real login. The Web API is the honest, working path.

## Prerequisites for control

1. A Spotify **Premium** account (the Web API playback endpoints return 403 for free accounts).
2. A free **Spotify developer app** (https://developer.spotify.com/dashboard): create one, copy its
   **Client ID**, and add this exact **Redirect URI**:

   ```
   http://127.0.0.1:8888/callback
   ```

3. Paste the Client ID into the add-on and press **Collega account**. The browser opens Spotify's
   consent page; approving it redirects to the loopback URI, which the add-on serves once to capture
   the authorization code. No client secret is used.

## OAuth flow (Authorization Code + PKCE)

- The add-on generates a PKCE `code_verifier` (64 random bytes, base64url) and
  `code_challenge = base64url(SHA256(verifier))`, opens
  `https://accounts.spotify.com/authorize?...&code_challenge_method=S256&scope=user-read-playback-state%20user-modify-playback-state`,
  and runs a one-shot loopback HTTP server on `127.0.0.1:8888` to catch `?code=`.
- It exchanges the code at `https://accounts.spotify.com/api/token` for an access + refresh token,
  and refreshes on demand. Only the **refresh token** and Client ID are persisted, under
  `<settings>/Campiello/spotify_auth` (owner-only `0600`). The Client ID is not a secret.
- Security note: the refresh token is stored in plaintext (protected by file permissions, like a
  keystore protects at rest but not against local code running as you). Encrypting it at rest with the
  AES-GCM scheme the SMB helper uses is a possible follow-up.

## Integration into Campiello

- `optional/spotify/SpotifyProbe.{h,cpp}`: the `getInfo` client over plain HTTP with a tiny JSON string
  extractor. `JsonString` is shared with the Web API module and unit-tested.
- `optional/spotify/SpotifyWebApi.{h,cpp}`: OAuth PKCE (libcrypto for SHA-256/base64url/random), the
  loopback redirect server, token exchange/refresh, and the `/v1/me/player` calls over **libcurl**
  (HTTPS). `ParseDevices`, `CodeChallenge`, `QueryParam`, `AuthorizeUrl` are unit-tested
  (`test_spotifyapi.cpp`, incl. the canonical RFC 7636 PKCE vector).
- `optional/spotify/campiello_spotify.cpp`: the window. `RefsReceived` reads `CAMPIELLO:host/name`, the
  SRV port from `CAMPIELLO:port` and `CAMPIELLO:txt.CPath`; it probes `getInfo`, and if an account is
  linked it lists devices, matches this speaker and shows playback controls. All network runs on worker
  threads. Links `libbe`, the network kit, `libcurl`, `libcrypto`, `liblocalestub`.
- `optional/spotify/spotify.handler`: matches `_spotify-connect._tcp`. `packaging/spotify` builds
  `campiello_spotify-0.1.0-5` and `requires lib:libcurl >= 4` + `lib:libcrypto >= 3`.

## Licensing

The add-on's own code is MIT. It links **libcurl** (curl license, permissive) and **libcrypto**
(Apache-2.0) dynamically at runtime, so it stays under `optional/` per the working agreement; the MIT
core does not depend on it.

## Reference material

- Spotify Web API: Authorization Code with PKCE; the `/v1/me/player` endpoints.
- Spotify "commercial hardware" ZeroConf API (the `getInfo` / `addUser` actions and the TXT keys).
- `librespot-org/librespot` for the zeroconf endpoint and field names.

## Testing status

- Unit tests pass: PKCE `code_challenge` against the RFC 7636 vector, base64url, the redirect
  query-param parser, the `/devices` JSON parser, and the authorize-URL builder.
- The **live** OAuth + control path needs a real Premium account and a developer app, so it is
  exercised by hand: link the account, press "Trasferisci qui" on a discovered speaker, then
  play/pause/next/volume. If the speaker is not listed by the account, open it once with the official
  Spotify app so it registers, then refresh.

## Follow-ups

- Encrypt the stored refresh token at rest (reuse the SMB AES-GCM keystore).
- Richer now-playing (album art, progress) and a device picker when the name match is ambiguous.
