// AfpProbe.h
//
// Unauthenticated probe for an AFP (Apple Filing Protocol) server discovered via _afpovertcp._tcp.
// AFP rides on DSI (Data Stream Interface) over TCP port 548. The DSIGetStatus command needs no
// session and no login: it returns the GetSrvrInfo block (server name, machine type, AFP versions,
// user-authentication methods). This add-on only reads that; it does NOT implement an AFP filesystem
// client (heavy: DSI sessions, the AFP command set, UAM authentication) and it notes that modern
// clients should use SMB (docs/addons/afp.md).
//
// Hand-rolled DSI over plain TCP: no third-party dependency (MIT-clean), links only libbe + the
// network kit.
//
// References: Apple "AFP 3.4" / DSI specification (the DSI header and DSIGetStatus / GetSrvrInfo
// layout) and the netatalk project (the open AFP server) for the same structures.

#ifndef CAMPIELLO_AFP_AFPPROBE_H
#define CAMPIELLO_AFP_AFPPROBE_H

#include <string>
#include <vector>

namespace campiello {
namespace afp {

struct Status {
	std::string              serverName;
	std::string              machineType;
	std::vector<std::string> versions; // AFP versions the server speaks
	std::vector<std::string> uams;     // user-authentication methods
};

class AfpProbe {
public:
	explicit AfpProbe(const std::string& host, int port = 548) : fHost(host), fPort(port) {}

	// Send DSIGetStatus and parse the reply. `okOut` reports whether the server answered.
	Status GetStatus(bool* okOut);

private:
	std::string fHost;
	int         fPort;
};

// GetSrvrInfo parsing (dependency-free; exposed for testing). `block` is the DSI payload.
Status ParseGetSrvrInfo(const std::string& block);
// Build the 16-byte DSIGetStatus request.
std::string BuildGetStatusRequest();

} // namespace afp
} // namespace campiello

#endif // CAMPIELLO_AFP_AFPPROBE_H
