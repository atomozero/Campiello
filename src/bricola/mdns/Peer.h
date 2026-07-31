// Peer.h
//
// A peer discovered on the LAN via DNS-SD, and the observer that Bricola notifies as peers
// appear, change, and leave. This is a discovery candidate, not a trusted identity: the
// friendly `instance` label and the `fingerprintHex` hint are attacker-controllable (mDNS is
// unauthenticated, docs/PROPOSAL.md section 9). Trust is conferred only later, by the TLS
// SPKI pinning in Traghetto; discovery merely proposes who to try.
//
// Pure standard C++ (no Haiku, no BeAPI), so the discovery logic is unit-testable off Haiku.

#ifndef CAMPIELLO_BRICOLA_MDNS_PEER_H
#define CAMPIELLO_BRICOLA_MDNS_PEER_H

#include <cstdint>
#include <string>
#include <vector>

namespace campiello {
namespace bricola {
namespace mdns {

struct Peer {
	std::string key;       // instance FQDN, the stable unique key, e.g. "Studio._campiello._tcp.local"
	std::string instance;  // friendly label shown to the user, e.g. "Studio" (key minus the service)
	std::string hostname;  // SRV target, e.g. "studio.local"
	std::vector<std::string> addresses; // resolved A records, numeric IPv4 (may fall back to sender IP)
	uint16_t    port = 0;               // SRV port (the CNP port to connect to)

	// From the TXT record. All advisory: never used to decide trust.
	uint8_t     protocolVersion = 0;    // TXT "v"
	bool        bfsAttrs = false;       // TXT "bfs" == "1"
	std::string caps;                   // TXT "caps"
	std::string fingerprintHex;         // TXT "fp" hint; cross-checked against the pinned key, never trusted

	int64_t     expiresAtMs = 0;        // table-managed: when the record set goes stale (monotonic ms)
};

// Bricola calls these as the peer set changes. Callbacks run on the discovery worker thread;
// the Haiku replicant adapter forwards them to the UI via a BMessenger (the worker never
// touches BViews). A peer is reported Found only once it is connectable (a port and at least
// one address are known); partial records are held back until then.
class PeerObserver {
public:
	virtual ~PeerObserver() = default;
	virtual void PeerFound(const Peer& peer)   = 0;
	virtual void PeerUpdated(const Peer& peer) = 0;
	virtual void PeerLost(const Peer& peer)    = 0;
};

} // namespace mdns
} // namespace bricola
} // namespace campiello

#endif // CAMPIELLO_BRICOLA_MDNS_PEER_H
