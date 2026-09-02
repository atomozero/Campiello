// SpotifyWebApi.cpp
//
// See SpotifyWebApi.h. OAuth Authorization Code + PKCE over a loopback redirect, plus the
// /v1/me/player Web API calls, using libcurl for HTTPS and libcrypto for PKCE.

#include "SpotifyWebApi.h"
#include "SpotifyProbe.h"   // JsonString

#include <cctype>
#include <cstdio>
#include <cstring>

#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <unistd.h>

#include <curl/curl.h>

#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/sha.h>

#include <FindDirectory.h>
#include <Path.h>
#include <Roster.h>

namespace campiello {
namespace spotify {

const char* const kRedirectUri  = "http://127.0.0.1:8888/callback";
const int         kRedirectPort = 8888;
const char* const kScope        = "user-read-playback-state user-modify-playback-state";

namespace {

const char* kTokenUrl  = "https://accounts.spotify.com/api/token";
const char* kApiBase   = "https://api.spotify.com";
const char* kAuthBase  = "https://accounts.spotify.com/authorize";

// ------------------------------------------------------------------ small utilities
std::string UrlEncode(const std::string& s)
{
	static const char* hex = "0123456789ABCDEF";
	std::string out;
	for (unsigned char c : s) {
		if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
			out += (char)c;
		else {
			out += '%';
			out += hex[c >> 4];
			out += hex[c & 0xF];
		}
	}
	return out;
}

struct HttpResp { long code = 0; std::string body; bool ok = false; };

size_t WriteCb(char* ptr, size_t sz, size_t n, void* ud)
{
	((std::string*)ud)->append(ptr, sz * n);
	return sz * n;
}

// One HTTPS request. `method` is GET/POST/PUT. `headers` are full header lines. Empty body allowed.
HttpResp HttpRequest(const std::string& method, const std::string& url,
	const std::vector<std::string>& headers, const std::string& body)
{
	HttpResp r;
	CURL* c = curl_easy_init();
	if (c == nullptr)
		return r;
	struct curl_slist* hl = nullptr;
	for (const auto& h : headers)
		hl = curl_slist_append(hl, h.c_str());

	curl_easy_setopt(c, CURLOPT_URL, url.c_str());
	if (method == "POST") {
		curl_easy_setopt(c, CURLOPT_POST, 1L);
		curl_easy_setopt(c, CURLOPT_POSTFIELDS, body.c_str());
		curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE, (long)body.size());
	} else if (method != "GET") {
		curl_easy_setopt(c, CURLOPT_CUSTOMREQUEST, method.c_str());
		// PUT/POST-like verbs may carry a JSON body.
		curl_easy_setopt(c, CURLOPT_POSTFIELDS, body.c_str());
		curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE, (long)body.size());
	}
	if (hl != nullptr)
		curl_easy_setopt(c, CURLOPT_HTTPHEADER, hl);
	curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, WriteCb);
	curl_easy_setopt(c, CURLOPT_WRITEDATA, &r.body);
	curl_easy_setopt(c, CURLOPT_TIMEOUT, 25L);
	curl_easy_setopt(c, CURLOPT_USERAGENT, "Campiello/1.0");

	CURLcode rc = curl_easy_perform(c);
	if (rc == CURLE_OK) {
		r.ok = true;
		curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &r.code);
	}
	if (hl != nullptr)
		curl_slist_free_all(hl);
	curl_easy_cleanup(c);
	return r;
}

// Read one integer field "key": <number> from a JSON object substring. Returns fallback if absent.
int JsonInt(const std::string& json, const std::string& key, int fallback)
{
	std::string needle = "\"" + key + "\"";
	size_t k = json.find(needle);
	if (k == std::string::npos)
		return fallback;
	size_t colon = json.find(':', k + needle.size());
	if (colon == std::string::npos)
		return fallback;
	size_t i = colon + 1;
	while (i < json.size() && (json[i] == ' ' || json[i] == '\t'))
		++i;
	bool neg = (i < json.size() && json[i] == '-');
	if (neg) ++i;
	if (i >= json.size() || !std::isdigit((unsigned char)json[i]))
		return fallback;
	int v = 0;
	while (i < json.size() && std::isdigit((unsigned char)json[i])) {
		v = v * 10 + (json[i] - '0');
		++i;
	}
	return neg ? -v : v;
}

// Read a boolean field "key": true/false. Returns fallback if absent.
bool JsonBool(const std::string& json, const std::string& key, bool fallback)
{
	std::string needle = "\"" + key + "\"";
	size_t k = json.find(needle);
	if (k == std::string::npos)
		return fallback;
	size_t colon = json.find(':', k + needle.size());
	if (colon == std::string::npos)
		return fallback;
	size_t t = json.find("true", colon);
	size_t f = json.find("false", colon);
	// whichever keyword appears first after the colon (within a short window)
	size_t limit = colon + 8;
	bool hasT = (t != std::string::npos && t <= limit);
	bool hasF = (f != std::string::npos && f <= limit);
	if (hasT && (!hasF || t < f)) return true;
	if (hasF) return false;
	return fallback;
}

std::string SettingsPath()
{
	char buf[B_PATH_NAME_LENGTH];
	if (find_directory(B_USER_SETTINGS_DIRECTORY, -1, false, buf, sizeof(buf)) != B_OK)
		return "";
	std::string dir = std::string(buf) + "/Campiello";
	return dir;
}

} // namespace

// ---------------------------------------------------------------- base64url / PKCE
std::string SpotifyWebApi::Base64Url(const unsigned char* data, size_t len)
{
	// EVP_EncodeBlock writes ceil(len/3)*4 + 1 bytes (NUL-terminated) standard base64.
	std::string out;
	out.resize(4 * ((len + 2) / 3) + 1);
	int n = EVP_EncodeBlock((unsigned char*)&out[0], data, (int)len);
	if (n < 0)
		return "";
	out.resize(n);
	for (char& c : out) {
		if (c == '+') c = '-';
		else if (c == '/') c = '_';
	}
	while (!out.empty() && out.back() == '=')
		out.pop_back();
	return out;
}

std::string SpotifyWebApi::RandomVerifier()
{
	unsigned char raw[64];
	if (RAND_bytes(raw, sizeof(raw)) != 1)
		return "";
	return Base64Url(raw, sizeof(raw));   // ~86 chars, within the 43-128 PKCE range
}

std::string SpotifyWebApi::CodeChallenge(const std::string& verifier)
{
	unsigned char digest[SHA256_DIGEST_LENGTH];
	SHA256((const unsigned char*)verifier.data(), verifier.size(), digest);
	return Base64Url(digest, sizeof(digest));
}

std::string SpotifyWebApi::AuthorizeUrl(const std::string& clientId, const std::string& challenge,
	const std::string& state)
{
	std::string u = kAuthBase;
	u += "?client_id=" + UrlEncode(clientId);
	u += "&response_type=code";
	u += "&redirect_uri=" + UrlEncode(kRedirectUri);
	u += "&code_challenge_method=S256";
	u += "&code_challenge=" + UrlEncode(challenge);
	u += "&scope=" + UrlEncode(kScope);
	u += "&state=" + UrlEncode(state);
	return u;
}

std::string SpotifyWebApi::QueryParam(const std::string& requestLine, const std::string& key)
{
	// requestLine like: GET /callback?code=ABC&state=XYZ HTTP/1.1
	std::string needle = key + "=";
	size_t q = requestLine.find('?');
	size_t from = (q == std::string::npos) ? 0 : q;
	size_t k = requestLine.find(needle, from);
	while (k != std::string::npos) {
		// must be preceded by ? or &
		char prev = (k > 0) ? requestLine[k - 1] : '?';
		if (prev == '?' || prev == '&')
			break;
		k = requestLine.find(needle, k + 1);
	}
	if (k == std::string::npos)
		return "";
	size_t start = k + needle.size();
	size_t end = start;
	while (end < requestLine.size() && requestLine[end] != '&' && requestLine[end] != ' '
		&& requestLine[end] != '\r' && requestLine[end] != '\n')
		++end;
	return requestLine.substr(start, end - start);
}

// ---------------------------------------------------------------- persistence
bool SpotifyWebApi::LoadAuth(AuthState& out)
{
	std::string dir = SettingsPath();
	if (dir.empty())
		return false;
	std::string path = dir + "/spotify_auth";
	FILE* f = std::fopen(path.c_str(), "r");
	if (f == nullptr)
		return false;
	char line[2048];
	while (std::fgets(line, sizeof(line), f) != nullptr) {
		std::string s(line);
		while (!s.empty() && (s.back() == '\n' || s.back() == '\r'))
			s.pop_back();
		size_t eq = s.find('=');
		if (eq == std::string::npos)
			continue;
		std::string k = s.substr(0, eq), v = s.substr(eq + 1);
		if (k == "clientId") out.clientId = v;
		else if (k == "refreshToken") out.refreshToken = v;
	}
	std::fclose(f);
	return out.HasRefresh();
}

bool SpotifyWebApi::SaveAuth(const AuthState& in)
{
	std::string dir = SettingsPath();
	if (dir.empty())
		return false;
	mkdir(dir.c_str(), 0755);
	std::string path = dir + "/spotify_auth";
	FILE* f = std::fopen(path.c_str(), "w");
	if (f == nullptr)
		return false;
	std::fprintf(f, "clientId=%s\n", in.clientId.c_str());
	std::fprintf(f, "refreshToken=%s\n", in.refreshToken.c_str());
	std::fclose(f);
	chmod(path.c_str(), 0600);
	return true;
}

void SpotifyWebApi::ClearAuth()
{
	std::string dir = SettingsPath();
	if (dir.empty())
		return;
	std::string path = dir + "/spotify_auth";
	unlink(path.c_str());
}

// ---------------------------------------------------------------- OAuth flow
namespace {

// Serve the loopback redirect for one request; returns the full HTTP request line (first line).
// Times out after `timeoutSec` seconds. Empty string on failure/timeout.
std::string ServeOneRedirect(int timeoutSec)
{
	int srv = socket(AF_INET, SOCK_STREAM, 0);
	if (srv < 0)
		return "";
	int yes = 1;
	setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
	struct sockaddr_in addr;
	std::memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons((uint16_t)kRedirectPort);
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	if (bind(srv, (struct sockaddr*)&addr, sizeof(addr)) != 0) { close(srv); return ""; }
	if (listen(srv, 1) != 0) { close(srv); return ""; }

	std::string requestLine;
	time_t deadline = time(nullptr) + timeoutSec;
	while (time(nullptr) < deadline) {
		fd_set rd;
		FD_ZERO(&rd);
		FD_SET(srv, &rd);
		struct timeval tv;
		tv.tv_sec = 2;
		tv.tv_usec = 0;
		int s = select(srv + 1, &rd, nullptr, nullptr, &tv);
		if (s <= 0)
			continue;
		int cli = accept(srv, nullptr, nullptr);
		if (cli < 0)
			continue;
		char buf[4096];
		ssize_t n = read(cli, buf, sizeof(buf) - 1);
		if (n > 0) {
			buf[n] = '\0';
			// first line only
			std::string all(buf);
			size_t eol = all.find('\n');
			requestLine = all.substr(0, (eol == std::string::npos) ? all.size() : eol);
		}
		const char* body =
			"<html><head><meta charset=\"utf-8\"><title>Campiello</title></head>"
			"<body style=\"font-family:sans-serif;padding:2em\">"
			"<h2>Campiello - Spotify</h2>"
			"<p>Autorizzazione ricevuta. Puoi chiudere questa finestra e tornare a Campiello.</p>"
			"</body></html>";
		char resp[1024];
		int rn = std::snprintf(resp, sizeof(resp),
			"HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\n"
			"Content-Length: %d\r\nConnection: close\r\n\r\n%s", (int)strlen(body), body);
		(void)write(cli, resp, rn);
		close(cli);
		if (requestLine.find("/callback") != std::string::npos
			&& (requestLine.find("code=") != std::string::npos
				|| requestLine.find("error=") != std::string::npos))
			break;
		requestLine.clear();   // e.g. a favicon request; keep waiting
	}
	close(srv);
	return requestLine;
}

} // namespace

bool SpotifyWebApi::Authorize(const std::string& clientId, AuthState& auth, std::string* err)
{
	auto fail = [&](const char* m) { if (err) *err = m; return false; };
	if (clientId.empty())
		return fail("client_id mancante");

	std::string verifier = RandomVerifier();
	std::string challenge = CodeChallenge(verifier);
	unsigned char sraw[16];
	RAND_bytes(sraw, sizeof(sraw));
	std::string state = Base64Url(sraw, sizeof(sraw));
	std::string url = AuthorizeUrl(clientId, challenge, state);

	// Open the browser. If none is registered the user can still paste the URL from the error text.
	if (be_roster != nullptr) {
		const char* args[] = { url.c_str() };
		if (be_roster->Launch("application/x-vnd.Be-URL.https", 1, const_cast<char**>(args)) != B_OK)
			be_roster->Launch("application/x-vnd.Be-URL.http", 1, const_cast<char**>(args));
	}

	std::string reqLine = ServeOneRedirect(180);
	if (reqLine.empty())
		return fail("Nessuna risposta di autorizzazione (timeout). Riprova.");
	if (QueryParam(reqLine, "error").size() > 0)
		return fail("Autorizzazione negata.");
	std::string code = QueryParam(reqLine, "code");
	std::string gotState = QueryParam(reqLine, "state");
	if (code.empty())
		return fail("Codice di autorizzazione mancante.");
	if (gotState != state)
		return fail("Stato OAuth non corrispondente (possibile manomissione).");

	// Exchange the code for tokens.
	std::string form;
	form += "grant_type=authorization_code";
	form += "&code=" + UrlEncode(code);
	form += "&redirect_uri=" + UrlEncode(kRedirectUri);
	form += "&client_id=" + UrlEncode(clientId);
	form += "&code_verifier=" + UrlEncode(verifier);
	HttpResp r = HttpRequest("POST", kTokenUrl,
		{ "Content-Type: application/x-www-form-urlencoded" }, form);
	if (!r.ok)
		return fail("Rete non disponibile durante lo scambio del token.");
	if (r.code != 200)
		return fail("Scambio del token fallito (controlla client_id e redirect URI).");

	auth.clientId = clientId;
	auth.accessToken = JsonString(r.body, "access_token");
	auth.refreshToken = JsonString(r.body, "refresh_token");
	int expires = JsonInt(r.body, "expires_in", 3600);
	auth.expiresAt = time(nullptr) + expires;
	if (auth.accessToken.empty() || auth.refreshToken.empty())
		return fail("Risposta del token incompleta.");
	SaveAuth(auth);
	return true;
}

bool SpotifyWebApi::EnsureToken(std::string* err)
{
	if (fAuth.TokenFresh())
		return true;
	if (!fAuth.HasRefresh()) {
		if (err) *err = "Account non collegato.";
		return false;
	}
	std::string form;
	form += "grant_type=refresh_token";
	form += "&refresh_token=" + UrlEncode(fAuth.refreshToken);
	form += "&client_id=" + UrlEncode(fAuth.clientId);
	HttpResp r = HttpRequest("POST", kTokenUrl,
		{ "Content-Type: application/x-www-form-urlencoded" }, form);
	if (!r.ok) { if (err) *err = "Rete non disponibile."; return false; }
	if (r.code != 200) { if (err) *err = "Rinnovo del token fallito; ricollega l'account."; return false; }
	fAuth.accessToken = JsonString(r.body, "access_token");
	std::string newRefresh = JsonString(r.body, "refresh_token");
	if (!newRefresh.empty())
		fAuth.refreshToken = newRefresh;   // Spotify may rotate it
	fAuth.expiresAt = time(nullptr) + JsonInt(r.body, "expires_in", 3600);
	if (fAuth.accessToken.empty()) { if (err) *err = "Token di accesso mancante."; return false; }
	SaveAuth(fAuth);
	return true;
}

// ---------------------------------------------------------------- Web API calls
long SpotifyWebApi::Api(const std::string& method, const std::string& url, const std::string& body,
	std::string& outBody, std::string* err)
{
	if (!EnsureToken(err))
		return 0;
	std::vector<std::string> headers = { "Authorization: Bearer " + fAuth.accessToken };
	if (!body.empty())
		headers.push_back("Content-Type: application/json");
	HttpResp r = HttpRequest(method, url, headers, body);
	if (!r.ok) { if (err) *err = "Rete non disponibile."; return 0; }
	// One retry if the token was rejected mid-flight.
	if (r.code == 401) {
		fAuth.expiresAt = 0;
		if (!EnsureToken(err))
			return 401;
		headers[0] = "Authorization: Bearer " + fAuth.accessToken;
		r = HttpRequest(method, url, headers, body);
		if (!r.ok) { if (err) *err = "Rete non disponibile."; return 0; }
	}
	outBody = r.body;
	if (r.code == 403 && err)
		*err = "Operazione non consentita (serve Spotify Premium).";
	else if (r.code == 404 && err)
		*err = "Nessuna sessione di riproduzione attiva.";
	return r.code;
}

bool SpotifyWebApi::GetDevices(std::vector<Device>& out, std::string* err)
{
	std::string body;
	long code = Api("GET", std::string(kApiBase) + "/v1/me/player/devices", "", body, err);
	if (code != 200)
		return false;
	out = ParseDevices(body);
	return true;
}

bool SpotifyWebApi::Transfer(const std::string& deviceId, bool startPlaying, std::string* err)
{
	std::string b = "{\"device_ids\":[\"" + deviceId + "\"],\"play\":"
		+ (startPlaying ? "true" : "false") + "}";
	std::string resp;
	long code = Api("PUT", std::string(kApiBase) + "/v1/me/player", b, resp, err);
	return code == 204 || code == 202 || code == 200;
}

bool SpotifyWebApi::Play(std::string* err)
{
	std::string resp;
	long code = Api("PUT", std::string(kApiBase) + "/v1/me/player/play", "", resp, err);
	return code == 204 || code == 202;
}

bool SpotifyWebApi::Pause(std::string* err)
{
	std::string resp;
	long code = Api("PUT", std::string(kApiBase) + "/v1/me/player/pause", "", resp, err);
	return code == 204 || code == 202;
}

bool SpotifyWebApi::Next(std::string* err)
{
	std::string resp;
	long code = Api("POST", std::string(kApiBase) + "/v1/me/player/next", "", resp, err);
	return code == 204 || code == 202;
}

bool SpotifyWebApi::Previous(std::string* err)
{
	std::string resp;
	long code = Api("POST", std::string(kApiBase) + "/v1/me/player/previous", "", resp, err);
	return code == 204 || code == 202;
}

bool SpotifyWebApi::SetVolume(int percent, std::string* err)
{
	if (percent < 0) percent = 0;
	if (percent > 100) percent = 100;
	std::string resp;
	std::string url = std::string(kApiBase) + "/v1/me/player/volume?volume_percent="
		+ std::to_string(percent);
	long code = Api("PUT", url, "", resp, err);
	return code == 204 || code == 202;
}

bool SpotifyWebApi::GetNowPlaying(NowPlaying& out, std::string* err)
{
	std::string body;
	long code = Api("GET", std::string(kApiBase) + "/v1/me/player", "", body, err);
	if (code == 204) { out = NowPlaying(); return true; }   // nothing playing
	if (code != 200)
		return false;
	out.playing = JsonBool(body, "is_playing", false);
	// device object: {"device":{"id":..,..}}
	size_t dev = body.find("\"device\"");
	if (dev != std::string::npos)
		out.deviceId = JsonString(body.substr(dev), "id");
	// item -> name + first artist name
	size_t item = body.find("\"item\"");
	if (item != std::string::npos && body.find("null", item) != item + 7) {
		std::string tail = body.substr(item);
		out.track = JsonString(tail, "name");
		size_t artists = tail.find("\"artists\"");
		if (artists != std::string::npos)
			out.artist = JsonString(tail.substr(artists), "name");
		out.haveTrack = !out.track.empty();
	}
	return true;
}

// ---------------------------------------------------------------- device array parse
std::vector<Device> ParseDevices(const std::string& json)
{
	std::vector<Device> out;
	size_t arr = json.find("\"devices\"");
	if (arr == std::string::npos)
		return out;
	size_t lb = json.find('[', arr);
	if (lb == std::string::npos)
		return out;
	int depth = 0;
	size_t objStart = std::string::npos;
	for (size_t i = lb; i < json.size(); ++i) {
		char c = json[i];
		if (c == '{') {
			if (depth == 0)
				objStart = i;
			++depth;
		} else if (c == '}') {
			--depth;
			if (depth == 0 && objStart != std::string::npos) {
				std::string obj = json.substr(objStart, i - objStart + 1);
				Device d;
				d.id     = JsonString(obj, "id");
				d.name   = JsonString(obj, "name");
				d.type   = JsonString(obj, "type");
				d.active = JsonBool(obj, "is_active", false);
				d.volume = JsonInt(obj, "volume_percent", -1);
				if (!d.id.empty() || !d.name.empty())
					out.push_back(d);
				objStart = std::string::npos;
			}
		} else if (c == ']' && depth == 0) {
			break;
		}
	}
	return out;
}

} // namespace spotify
} // namespace campiello
