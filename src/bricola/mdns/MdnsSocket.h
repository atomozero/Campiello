// MdnsSocket.h
//
// The multicast UDP socket Bricola uses to speak mDNS: it joins the mDNS group
// 224.0.0.251:5353, sends datagrams to the group, and receives datagrams (from any peer or
// its own loopback) with a timeout. One socket serves both the responder and the browser,
// exactly as a real mDNS node does.
//
// Socket options verified against the installed Haiku headers (docs working agreement rule
// 1): SO_REUSEADDR/SO_REUSEPORT (posix/sys/socket.h), IP_ADD_MEMBERSHIP / IP_MULTICAST_TTL /
// IP_MULTICAST_LOOP and struct ip_mreq (posix/netinet/in.h). These are standard BSD-socket
// names, so the code also builds and its loopback test runs off Haiku (Linux CI), per
// PROPOSAL.md section 15.
//
// Blocking receive with an explicit timeout; the Bricola worker thread owns one MdnsSocket
// and drives it. No Haiku or BeAPI dependency.

#ifndef CAMPIELLO_BRICOLA_MDNS_MDNSSOCKET_H
#define CAMPIELLO_BRICOLA_MDNS_MDNSSOCKET_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace campiello {
namespace bricola {
namespace mdns {

// The link-local mDNS group and port (RFC 6762 section 3).
extern const char* const kMdnsGroup;   // "224.0.0.251"
static const uint16_t kMdnsPort = 5353;

// Enumerate this host's up, AF_INET interface addresses (numeric IPv4). `includeLoopback`
// keeps 127.0.0.1. Uses SIOCGIFCONF/SIOCGIFFLAGS (verified against posix/net/if.h). Note: on
// Haiku the IFF_MULTICAST flag is not set even on interfaces that carry multicast (confirmed:
// loopback multicast works with the flag clear), so it is NOT used as a filter.
std::vector<std::string> LocalIPv4Addresses(bool includeLoopback);

// The interface Bricola should bind multicast to: the first up, non-loopback IPv4, or an empty
// string if the host has only loopback. Binding to a concrete interface is required on Haiku,
// where there is no default route for 224.0.0.0/4, so INADDR_ANY does not deliver.
std::string PrimaryMulticastIPv4();

class MdnsSocket {
public:
	MdnsSocket() = default;
	~MdnsSocket();
	MdnsSocket(const MdnsSocket&) = delete;
	MdnsSocket& operator=(const MdnsSocket&) = delete;

	// Create the socket, allow port reuse, bind to the mDNS port, join the group, set multicast
	// TTL to 1 (stay on the LAN) and loop on (so same-host peers and tests receive). Returns
	// false on any step's failure (see Error()); leaves the object closed.
	//
	// `interfaceIpv4` selects the interface to send and receive multicast on (its numeric IPv4,
	// e.g. "192.168.2.100", or "127.0.0.1" for same-host-only). Pass null/empty for INADDR_ANY,
	// which on Haiku does NOT deliver (no default multicast route), so real use should pass a
	// concrete interface (see PrimaryMulticastIPv4).
	bool Open(const char* interfaceIpv4 = nullptr);

	// Drop the group membership and close the fd. Idempotent; also called by the destructor.
	void Close();

	bool IsOpen() const { return fFd >= 0; }

	// Send a datagram to the mDNS group (224.0.0.251:5353). False on I/O error.
	bool SendMulticast(const void* data, size_t len);

	// Send a datagram to a specific unicast address (numeric IPv4), for the unicast-reply
	// path (RFC 6762 section 5.4). False on bad address or I/O error.
	bool SendTo(const char* ipv4, uint16_t port, const void* data, size_t len);

	// Wait up to timeoutMs for a datagram. Returns the byte count (>0) and fills srcIp/srcPort
	// with the sender; 0 on timeout; -1 on error. A datagram larger than bufLen is truncated
	// to bufLen (mDNS packets fit well under a normal buffer).
	int Receive(void* buf, size_t bufLen, int timeoutMs, std::string& srcIp, uint16_t& srcPort);

	// The raw fd, so the worker can multiplex it with other waits later. -1 when closed.
	int Fd() const { return fFd; }

	// Human-readable reason for the last failure, developer log only.
	const char* Error() const { return fError; }

private:
	int         fFd    = -1;
	const char* fError = nullptr;
	std::string fInterface;   // chosen interface IPv4 (empty = INADDR_ANY), for symmetric leave
};

} // namespace mdns
} // namespace bricola
} // namespace campiello

#endif // CAMPIELLO_BRICOLA_MDNS_MDNSSOCKET_H
