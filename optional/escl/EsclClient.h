// EsclClient.h
//
// Minimal eSCL (Apple AirScan) client for a network scanner discovered via _uscan._tcp. eSCL is an
// XML-over-HTTP scanning protocol. Workflow:
//   GET  http://IP:port/eSCL/ScannerCapabilities            -> XML capabilities
//   POST http://IP:port/eSCL/ScanJobs   (ScanSettings XML)  -> 201, Location: .../ScanJobs/<uuid>
//   GET  <jobUri>/NextDocument                              -> 200 image bytes (202 = in progress)
//
// Plain HTTP (no TLS for _uscan._tcp), encoded by hand, so this module needs no third-party
// dependency: MIT-clean, links only libbe + the network kit. The scanner's real port and resource
// path come from the mDNS SRV/TXT (rs); defaulted here (port 80, "eSCL") pending TXT plumbing.
//
// References: alexpevzner/sane-airscan (the reference eSCL/WSD driver) and the "Reverse Engineering
// eSCL / Apple AirScan" write-up cross-checked the endpoints, the 201/Location flow, and the
// 200/202 NextDocument polling.

#ifndef CAMPIELLO_ESCL_ESCLCLIENT_H
#define CAMPIELLO_ESCL_ESCLCLIENT_H

#include <string>
#include <utility>
#include <vector>

namespace campiello {
namespace escl {

struct ScanOptions {
	int         resolution = 300;         // dpi
	std::string colorMode = "RGB24";      // RGB24 | Grayscale8
	std::string source = "Platen";        // Platen | Feeder
	std::string format = "image/jpeg";    // image/jpeg | application/pdf
	int         widthPx = 2480;           // A4 at 300 dpi
	int         heightPx = 3508;
};

class EsclClient {
public:
	explicit EsclClient(const std::string& host, int port = 80, const std::string& rs = "eSCL")
		: fHost(host), fPort(port), fRs(rs) {}

	// Query the scanner. Returns decoded capability (name, value) pairs (empty on failure); `okOut`
	// reports HTTP success.
	std::vector<std::pair<std::string, std::string>> GetCapabilities(bool* okOut);

	// Scan one page and write it to `destPath`. Returns true on success. Posts ScanSettings, follows
	// the Location header, and polls NextDocument until the image arrives.
	bool Scan(const std::string& destPath, const ScanOptions& opt);

	std::string BaseUrl() const;

private:
	struct Response { int status = 0; std::string location; std::string body; };
	Response HttpRequest(const std::string& method, const std::string& path,
		const std::string& contentType, const std::string& body);

	std::string fHost;
	int         fPort;
	std::string fRs;
};

// XML helpers (dependency-free; exposed for testing).
namespace xml {
	// Value of the first <...:Tag> or <Tag> element (namespace-prefix agnostic). Empty if absent.
	std::string Tag(const std::string& doc, const std::string& tag);
	// All values of a repeated element.
	std::vector<std::string> Tags(const std::string& doc, const std::string& tag);
	// Build a pwg/scan ScanSettings request body.
	std::string BuildScanSettings(const ScanOptions& opt);
}

} // namespace escl
} // namespace campiello

#endif // CAMPIELLO_ESCL_ESCLCLIENT_H
