// FireTVRemote.h
//
// Native C++ client for Amazon Fire TV over its official REST API (no ADB). Ported from the
// validated prototype (firetv_rest.py / FireTVRemote.cpp). Protocol:
//   wake   : POST http://IP:8009/apps/FireTVRemote            (plain HTTP; opens the 8080 service)
//   pin    : POST https://IP:8080/v1/FireTV/pin/display   {"friendlyName":"..."}
//   verify : POST https://IP:8080/v1/FireTV/pin/verify    {"pin":"1234"} -> token in "description"
//   nav    : POST https://IP:8080/v1/FireTV?action=<a>    {"keyActionType":"keyDownUp"}
//   media  : POST https://IP:8080/v1/media?action=<a>
//   app    : POST https://IP:8080/v1/FireTV/app/<package>
//   text   : POST https://IP:8080/v1/FireTV/text          {"text":"..."}
//
// OPTIONAL, Haiku: links OpenSSL (libssl/libcrypto, Apache-2.0) and the network kit. Ships in the
// separate campiello_firetv package; the MIT core never depends on it. The Fire TV's TLS cert is
// self-signed, so the client does not verify it (LAN device, paired by PIN).

#ifndef CAMPIELLO_FIRETV_FIRETVREMOTE_H
#define CAMPIELLO_FIRETV_FIRETVREMOTE_H

#include <string>

namespace campiello {
namespace firetv {

class FireTVRemote {
public:
	explicit FireTVRemote(const std::string& ip, const std::string& token = "")
		: fIp(ip), fToken(token) {}

	// Wake the device / start the remote service (opens port 8080). Safe to call before any action.
	bool Wake();

	// Pairing: show a PIN on the TV, then verify it to obtain a client token (empty on failure).
	bool RequestPin(const std::string& friendlyName);
	std::string VerifyPin(const std::string& pin);

	// Controls. `action` values: up/down/left/right/select/home/back/menu/... (nav); play/pause/...
	// (media). `pkg` is an app package id; `text` types a string on the current field.
	bool Nav(const std::string& action);
	bool Media(const std::string& action, const std::string& direction = "");
	bool Launch(const std::string& pkg);
	bool Text(const std::string& text);

	const std::string& Token() const { return fToken; }
	void SetToken(const std::string& token) { fToken = token; }

private:
	int TcpConnect(int port);
	std::string PlainHttp(int port, const std::string& request, int* statusOut);
	std::string HttpsPost(const std::string& path, const std::string& jsonBody, bool withToken,
		int* statusOut);

	std::string fIp;
	std::string fToken;
};

// JSON helpers shared with the app (small, dependency-free).
std::string JsonEscape(const std::string& s);
std::string ExtractJsonString(const std::string& json, const std::string& key);

} // namespace firetv
} // namespace campiello

#endif // CAMPIELLO_FIRETV_FIRETVREMOTE_H
