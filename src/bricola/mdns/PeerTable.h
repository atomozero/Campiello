// PeerTable.h
//
// The reconciliation and lifetime state machine behind Bricola's browser. It accumulates the
// DNS-SD records that describe peers (which arrive spread across packets and over time) into
// whole Peer entries, and reports appearances, changes, and departures to a PeerObserver.
//
// Reconciliation: a PTR names an instance, an SRV gives that instance its host + port, a TXT
// its attributes, and an A resolves a host to an address. PeerTable keys peers by instance
// FQDN and keeps a host -> address map so A records attach to the right peers whatever order
// they arrive in. If no A resolves a peer's host, the packet's sender address is used as a
// fallback (an mDNS response comes from the peer itself).
//
// Lifetime: each record refreshes the peer's expiry from its TTL; a record with TTL 0 is a
// DNS-SD goodbye and removes the peer at once (RFC 6762 section 10.1). Expire() drops peers
// whose records have all gone stale. A peer is announced (PeerFound) only once it is
// connectable (has a port and at least one address); until then it is pending and silent.
//
// The clock is passed in as monotonic milliseconds per call, so behavior is deterministic in
// tests without sleeping. Not thread-safe: the discovery worker serializes access.
//
// Pure standard C++ (no Haiku, no BeAPI).

#ifndef CAMPIELLO_BRICOLA_MDNS_PEERTABLE_H
#define CAMPIELLO_BRICOLA_MDNS_PEERTABLE_H

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "MdnsWire.h"
#include "Peer.h"

namespace campiello {
namespace bricola {
namespace mdns {

class PeerTable {
public:
	// `serviceName` is the DNS-SD type we track, e.g. "_campiello._tcp.local"; PTR answers for
	// any other service are ignored. `observer` may be null (then the table just accumulates).
	PeerTable(std::string serviceName, PeerObserver* observer);

	// Fold one received datagram into the table. The raw buffer is parsed here so the typed
	// decoders can follow DNS compression pointers in PTR/SRV targets (third-party responders
	// compress; ours does not). `srcIp` is the datagram's sender (numeric IPv4, used as an
	// address fallback); `nowMs` is monotonic milliseconds. A malformed packet is ignored.
	void Ingest(const uint8_t* buf, size_t len, const std::string& srcIp, int64_t nowMs);

	// Drop peers whose records have all expired as of `nowMs`, firing PeerLost for announced
	// ones.
	void Expire(int64_t nowMs);

	// A snapshot of the currently announced (connectable) peers.
	std::vector<Peer> Peers() const;

	// Count including pending (not-yet-connectable) peers; for tests and diagnostics.
	size_t Size() const { return fPeers.size(); }

private:
	struct Entry {
		Peer peer;
		bool announced = false; // whether PeerFound has fired (i.e. it became connectable)
	};

	// Strip the ".<serviceName>" suffix to get the friendly instance label.
	std::string FriendlyLabel(const std::string& instanceFqdn) const;

	// Attach any known addresses for a host to a peer; returns true if its address set changed.
	bool AttachAddresses(Peer& peer);

	// After mutating an entry, fire the right observer callback (found/updated) if warranted.
	void Publish(Entry& entry);

	std::string   fService;
	PeerObserver* fObserver;
	std::map<std::string, Entry> fPeers;         // key: instance FQDN
	std::map<std::string, std::set<std::string>> fHostAddrs; // host (lowercased) -> IPv4 set
};

} // namespace mdns
} // namespace bricola
} // namespace campiello

#endif // CAMPIELLO_BRICOLA_MDNS_PEERTABLE_H
