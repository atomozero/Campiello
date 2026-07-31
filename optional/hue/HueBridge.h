// HueBridge.h
//
// Native C++ client for a Philips Hue bridge over its local REST API (no cloud). Protocol (verified
// against the public Hue developer docs and community references, e.g. github.com/tigoe/hue-control):
//   pair   : POST https://IP/api            {"devicetype":"campiello#haiku","generateclientkey":true}
//            -> [{"success":{"username":"<app-key>",...}}] once the bridge link button is pressed,
//               or [{"error":{"type":101,"description":"link button not pressed"}}]
//   lights : GET  https://IP/clip/v2/resource/light      header: hue-application-key: <app-key>
//   set    : PUT  https://IP/clip/v2/resource/light/<id>  {"on":{"on":true}} / {"dimming":{...}}
//
// The bridge uses a self-signed TLS certificate on port 443, so the client does not verify it (LAN
// device, authorized by the physical link button). OPTIONAL, Haiku: links OpenSSL (libssl/libcrypto,
// Apache-2.0) and the network kit; ships in the separate campiello_hue package. The MIT core never
// depends on it.

#ifndef CAMPIELLO_HUE_HUEBRIDGE_H
#define CAMPIELLO_HUE_HUEBRIDGE_H

#include <string>
#include <vector>

namespace campiello {
namespace hue {

// One controllable light, as returned by the v2 light resource.
struct Light {
	std::string id;         // v2 resource id (UUID) used in the PUT path
	std::string name;       // metadata.name
	bool        on = false; // on.on
	int         brightness = 0; // dimming.brightness, 0-100 (0 if the light is not dimmable)
	bool        dimmable = false;
};

class HueBridge {
public:
	explicit HueBridge(const std::string& ip, const std::string& appKey = "")
		: fIp(ip), fAppKey(appKey) {}

	// Pairing: call after the user presses the bridge's round link button. Returns the application
	// key (empty if the button was not pressed or on error); on success the key is also stored.
	std::string Pair(const std::string& appName);

	// List the bridge's lights (empty on error / not paired).
	std::vector<Light> ListLights();

	// Controls. `id` is a Light::id. Brightness is a percentage 0-100.
	bool SetOn(const std::string& id, bool on);
	bool SetBrightness(const std::string& id, int percent);

	const std::string& AppKey() const { return fAppKey; }
	void SetAppKey(const std::string& key) { fAppKey = key; }

private:
	// method: "GET"|"POST"|"PUT". Sends the hue-application-key header when withKey and a key is set.
	// Returns the response body; *statusOut gets the HTTP status (0 on transport failure).
	std::string HttpsRequest(const std::string& method, const std::string& path,
		const std::string& body, bool withKey, int* statusOut);

	std::string fIp;
	std::string fAppKey;
};

// Small dependency-free JSON helpers (shared with the app).
std::string JsonEscape(const std::string& s);
std::string ExtractJsonString(const std::string& json, const std::string& key);
// Split the "data" array of a Hue v2 response into its top-level object substrings.
std::vector<std::string> SplitJsonDataObjects(const std::string& json);

} // namespace hue
} // namespace campiello

#endif // CAMPIELLO_HUE_HUEBRIDGE_H
