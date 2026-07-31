// Responder.h
//
// The advertising half of Bricola: it describes this node as a DNS-SD service and answers
// browsers. Given a ServiceInfo (our instance label, host, address, CNP port, and the TXT
// facts), it builds the PTR + SRV + TXT + A record set that announces us, the answer to a
// matching service query, and the TTL-0 goodbye we multicast on shutdown.
//
// M2 scope is the pragmatic responder chosen in the design note: announce unsolicited on a
// timer, answer service-type PTR queries, and say goodbye on stop. It does NOT probe for name
// uniqueness or resolve conflicts (RFC 6762 sections 8-9); the MdnsWire codec is built to
// allow adding that later. Name disambiguation between two nodes with the same friendly label
// leans on the fp TXT hint for now.
//
// Like Browser, it owns no socket and no thread: it returns packets for the Bricola facade to
// send. That keeps the whole advertise path unit-testable off Haiku. Pure standard C++.

#ifndef CAMPIELLO_BRICOLA_MDNS_RESPONDER_H
#define CAMPIELLO_BRICOLA_MDNS_RESPONDER_H

#include <cstddef>
#include <cstdint>
#include <string>

#include "Browser.h"   // kCampielloService

namespace campiello {
namespace bricola {
namespace mdns {

// What this node advertises about itself. Mirrors the TXT keys in PROPOSAL.md section 9.
struct ServiceInfo {
	std::string instance;        // friendly label, e.g. "Studio" (becomes "<instance>.<service>")
	std::string hostname;        // e.g. "studio.local"
	std::string address;         // our numeric IPv4 for the A record, e.g. "192.168.1.7"
	uint16_t    port = 0;        // the CNP port peers connect to (ServerNode::Port())
	uint8_t     protocolVersion = 0; // TXT "v"
	bool        bfsAttrs = true;     // TXT "bfs"
	std::string caps;                // TXT "caps" (omitted when empty)
	std::string fingerprintHex;      // TXT "fp" hint (omitted when empty)
};

class Responder {
public:
	explicit Responder(ServiceInfo self, std::string serviceName = kCampielloService);

	// The unsolicited announce: PTR (answer) plus SRV/TXT/A (additionals) describing us, at
	// the normal DNS-SD TTLs. Sent on startup and periodically.
	std::string AnnouncePacket() const;

	// The goodbye: our PTR with TTL 0, telling browsers to drop us at once (RFC 6762 10.1).
	std::string GoodbyePacket() const;

	// If `buf` is a query asking for our service (a PTR/ANY question for the service name),
	// returns the answer to multicast; otherwise an empty string (nothing to send). Returns
	// empty while unconfigured (no port), since there is nothing yet to advertise.
	std::string ResponseTo(const uint8_t* buf, size_t len) const;

	// Fill in the real bound port / address once the server is up.
	void SetPort(uint16_t port) { fInfo.port = port; }
	void SetAddress(const std::string& ipv4) { fInfo.address = ipv4; }

	const ServiceInfo& Info() const { return fInfo; }

	// The instance FQDN we register, "<instance>.<service>".
	std::string InstanceFqdn() const;

private:
	ServiceInfo fInfo;
	std::string fService;
};

} // namespace mdns
} // namespace bricola
} // namespace campiello

#endif // CAMPIELLO_BRICOLA_MDNS_RESPONDER_H
