// SpotifyWebApi.h
//
// Active control of a Spotify Connect device through the Spotify Web API (the cloud path). Unlike
// SpotifyProbe (the unauthenticated local getInfo), this authenticates the user with OAuth and can
// actually transfer playback to a device and drive play/pause/next/previous/volume.
//
// Why the Web API and not the local ZeroConf addUser: logging a user straight into the device needs a
// Spotify credentials blob that is only obtainable through the full librespot-style authentication
// (Shannon cipher + protobuf + Mercury), which is not feasible dependency-free. The Web API is the
// realistic, honest path; it needs a Spotify Premium account and a (free) Spotify developer app.
//
// OAuth: Authorization Code + PKCE (no client secret). The redirect is a loopback URI
// (http://127.0.0.1:8888/callback) that this process serves for one request to catch the code; the
// user authorizes once in a browser. The refresh token is stored under
// <settings>/Campiello/spotify_auth (owner-only 0600). client_id is not a secret.
//
// Transport is libcurl (HTTPS); PKCE/base64url/SHA-256/random come from libcrypto. JSON is read with
// the tiny JsonString extractor shared with SpotifyProbe. This is optional/, so the LGPL/permissive
// runtime deps (libcurl is the curl license, libcrypto Apache-2.0) stay out of the MIT core.
//
// References: Spotify Web API (Authorization Code with PKCE; /v1/me/player endpoints).

#ifndef CAMPIELLO_SPOTIFY_SPOTIFYWEBAPI_H
#define CAMPIELLO_SPOTIFY_SPOTIFYWEBAPI_H

#include <ctime>
#include <string>
#include <vector>

namespace campiello {
namespace spotify {

// The loopback redirect the user must register in their Spotify developer app.
extern const char* const kRedirectUri;   // "http://127.0.0.1:8888/callback"
extern const int         kRedirectPort;  // 8888
extern const char* const kScope;         // "user-read-playback-state user-modify-playback-state"

struct Device {
	std::string id;
	std::string name;
	std::string type;         // e.g. Speaker, Computer, Smartphone
	bool        active = false;
	int         volume = -1;  // percent, -1 if unknown
};

struct NowPlaying {
	bool        playing = false;
	std::string track;
	std::string artist;
	std::string deviceId;     // the id of the currently active device, if any
	bool        haveTrack = false;
};

// Persisted credentials + the live access token.
struct AuthState {
	std::string clientId;
	std::string accessToken;
	std::string refreshToken;
	std::time_t expiresAt = 0;   // absolute unix time the access token stops being valid

	bool HasRefresh() const { return !clientId.empty() && !refreshToken.empty(); }
	bool TokenFresh() const { return !accessToken.empty() && std::time(nullptr) < expiresAt - 30; }
};

class SpotifyWebApi {
public:
	explicit SpotifyWebApi(AuthState auth) : fAuth(std::move(auth)) {}

	// ---- persistence (<settings>/Campiello/spotify_auth, 0600) -------------------
	static bool LoadAuth(AuthState& out);       // false if no stored client_id/refresh
	static bool SaveAuth(const AuthState& in);
	static void ClearAuth();

	// ---- PKCE / URL helpers (pure; exposed for unit tests) -----------------------
	static std::string RandomVerifier();                              // base64url, 43-128 chars
	static std::string CodeChallenge(const std::string& verifier);   // base64url(SHA256(verifier))
	static std::string AuthorizeUrl(const std::string& clientId, const std::string& challenge,
		const std::string& state);
	static std::string Base64Url(const unsigned char* data, size_t len);
	// Extract query parameter `key` from an HTTP request line like "GET /callback?code=..&state=..".
	static std::string QueryParam(const std::string& requestLine, const std::string& key);

	// ---- OAuth flow (blocking; run on a worker thread) ---------------------------
	// Opens the browser to the authorize URL, serves the loopback redirect once, exchanges the code
	// for tokens. On success fills `auth` (clientId + tokens) and returns true.
	static bool Authorize(const std::string& clientId, AuthState& auth, std::string* err);

	// Refresh the access token if missing/expired (uses the stored refresh token). Persists the
	// refreshed state. Returns false with *err set if it cannot obtain a usable token.
	bool EnsureToken(std::string* err);

	const AuthState& Auth() const { return fAuth; }

	// ---- Web API actions (each ensures a fresh token first) ----------------------
	bool GetDevices(std::vector<Device>& out, std::string* err);
	bool Transfer(const std::string& deviceId, bool startPlaying, std::string* err);
	bool Play(std::string* err);
	bool Pause(std::string* err);
	bool Next(std::string* err);
	bool Previous(std::string* err);
	bool SetVolume(int percent, std::string* err);
	bool GetNowPlaying(NowPlaying& out, std::string* err);

private:
	// Authenticated Web API request; returns HTTP status and fills body. Refreshes token on demand.
	long Api(const std::string& method, const std::string& url, const std::string& body,
		std::string& outBody, std::string* err);

	AuthState fAuth;
};

// Parse the "devices" array from a /v1/me/player/devices JSON reply.
std::vector<Device> ParseDevices(const std::string& json);

} // namespace spotify
} // namespace campiello

#endif // CAMPIELLO_SPOTIFY_SPOTIFYWEBAPI_H
