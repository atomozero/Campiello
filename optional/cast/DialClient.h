// DialClient.h
//
// Minimal DIAL (DIscovery And Launch) client for a Google Cast / Chromecast device discovered via
// _googlecast._tcp. DIAL is a plain-HTTP REST protocol (jointly by Netflix and YouTube) for
// launching and stopping receiver apps:
//   GET    http://IP:8008/ssdp/device-desc.xml   -> device description (friendlyName)
//   GET    http://IP:8008/apps/<AppName>         -> app state XML (<state>running|stopped</state>)
//   POST   http://IP:8008/apps/<AppName>         -> launch the app (201 Created, Location: .../run)
//   DELETE http://IP:8008/apps/<AppName>/run     -> stop the running app
//
// Plain HTTP, encoded by hand: no third-party dependency (MIT-clean), links only libbe + the network
// kit. This covers launch/stop/status. Full media casting (load a video URL, transport controls) uses
// the CASTv2 protobuf channel over TLS on port 8009, which is a heavier follow-up (docs/addons/cast.md).
//
// References: the DIAL specification (dial-multiscreen.org, summarized on Wikipedia "Discovery and
// Launch") and github.com/geraldnilles/Chromecast-Server, which document the port 8008 REST endpoints.

#ifndef CAMPIELLO_CAST_DIALCLIENT_H
#define CAMPIELLO_CAST_DIALCLIENT_H

#include <string>

namespace campiello {
namespace cast {

class DialClient {
public:
	explicit DialClient(const std::string& host, int port = 8008) : fHost(host), fPort(port) {}

	// The device's friendly name (empty on failure).
	std::string FriendlyName();

	// App state: "running", "stopped", "" (not installed / unreachable). `okOut` is HTTP success.
	std::string AppState(const std::string& appName, bool* okOut);

	// Launch / stop a receiver app. Return true on a successful HTTP status.
	bool Launch(const std::string& appName);
	bool Stop(const std::string& appName);

private:
	struct Response { int status = 0; std::string location; std::string body; };
	Response HttpRequest(const std::string& method, const std::string& path, const std::string& body);

	std::string fHost;
	int         fPort;
};

// Value of the first <...:Tag> or <Tag> element (namespace-prefix agnostic). Empty if absent.
std::string XmlTag(const std::string& doc, const std::string& tag);

} // namespace cast
} // namespace campiello

#endif // CAMPIELLO_CAST_DIALCLIENT_H
