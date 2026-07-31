// WebDavClient.h
//
// Minimal WebDAV client (RFC 4918) for a file server discovered via _webdav._tcp. WebDAV extends
// HTTP: PROPFIND with "Depth: 1" returns a 207 Multistatus XML listing a collection's members; GET
// downloads a resource. Optional HTTP Basic authentication.
//
// Pure HTTP + a hand-rolled XML parser: no third-party dependency (MIT-clean), links only libbe + the
// network kit. Plain HTTP (_webdav._tcp); HTTPS WebDAV (_webdavs._tcp) is a documented follow-up.
//
// Reference: RFC 4918 (HTTP Extensions for Web Distributed Authoring and Versioning) for PROPFIND,
// the multistatus response, and the DAV: property names used here.

#ifndef CAMPIELLO_WEBDAV_WEBDAVCLIENT_H
#define CAMPIELLO_WEBDAV_WEBDAVCLIENT_H

#include <string>
#include <vector>

namespace campiello {
namespace webdav {

struct Resource {
	std::string name;   // displayname, or the last path segment of href
	std::string href;   // server path, used to navigate / download
	bool        isDir = false;
	long long   size = 0;
};

class WebDavClient {
public:
	WebDavClient(const std::string& host, int port = 80,
		const std::string& user = "", const std::string& pass = "")
		: fHost(host), fPort(port), fUser(user), fPass(pass) {}

	// List the members of a collection (Depth: 1). `okOut` reports HTTP/DAV success.
	std::vector<Resource> List(const std::string& path, bool* okOut);

	// Download a resource to a local file. Returns true on success.
	bool Download(const std::string& href, const std::string& localPath);

private:
	// method: "PROPFIND"|"GET". `depth`<0 omits the Depth header. Returns the response body;
	// *status gets the HTTP status.
	std::string HttpRequest(const std::string& method, const std::string& path, int depth,
		const std::string& body, int* status);

	std::string fHost;
	int fPort;
	std::string fUser, fPass;
};

// XML helpers for the multistatus response (dependency-free; exposed for testing).
namespace xml {
	// Inner contents of each <...:tag> ... </...:tag> block (namespace-prefix agnostic).
	std::vector<std::string> Blocks(const std::string& doc, const std::string& tag);
	// Value of the first <...:tag> element (namespace-prefix agnostic). Empty if absent.
	std::string Tag(const std::string& doc, const std::string& tag);
	// Parse a multistatus body into resources, skipping the collection's own entry (`selfPath`).
	std::vector<Resource> ParseMultistatus(const std::string& body, const std::string& selfPath);
	// Percent-decode an href path segment.
	std::string UrlDecode(const std::string& s);
}

} // namespace webdav
} // namespace campiello

#endif // CAMPIELLO_WEBDAV_WEBDAVCLIENT_H
