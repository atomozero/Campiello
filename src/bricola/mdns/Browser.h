// Browser.h
//
// The DNS-SD browsing half of Bricola: it asks "who serves _campiello._tcp?" and turns the
// answers into live Peer events. Browser is the thin seam between raw datagrams and the
// PeerTable reconciler; it holds no socket and no thread of its own. The Bricola facade owns
// the MdnsSocket and the worker loop, and drives a Browser like this:
//
//   sock.SendMulticast(browser.QueryPacket());          // periodically, and on startup
//   n = sock.Receive(buf, ..., srcIp, srcPort);
//   if (n > 0) browser.OnPacket(buf, n, srcIp, nowMs);   // for each datagram
//   browser.Tick(nowMs);                                 // periodically, to expire stale peers
//
// Keeping the socket and clock outside makes the whole browse/reconcile path unit-testable
// off Haiku by feeding it packets built with MdnsWire. Pure standard C++.

#ifndef CAMPIELLO_BRICOLA_MDNS_BROWSER_H
#define CAMPIELLO_BRICOLA_MDNS_BROWSER_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "Peer.h"
#include "PeerTable.h"

namespace campiello {
namespace bricola {
namespace mdns {

// The DNS-SD service type Campiello browses and advertises.
extern const char* const kCampielloService;   // "_campiello._tcp.local"

class Browser {
public:
	// `serviceName` is the DNS-SD type to browse (defaults to _campiello._tcp.local);
	// `observer` receives Found/Updated/Lost and may be null.
	explicit Browser(PeerObserver* observer, std::string serviceName = kCampielloService);

	// The PTR query datagram to multicast to ask who offers the service.
	std::string QueryPacket() const;

	// Feed one received datagram (raw bytes + sender IP) at monotonic time `nowMs`.
	void OnPacket(const uint8_t* buf, size_t len, const std::string& srcIp, int64_t nowMs);

	// Expire peers whose records have gone stale as of `nowMs`.
	void Tick(int64_t nowMs);

	// A snapshot of the currently connectable peers (for a late UI subscriber).
	std::vector<Peer> Peers() const { return fTable.Peers(); }

private:
	std::string fService;
	PeerTable   fTable;
};

} // namespace mdns
} // namespace bricola
} // namespace campiello

#endif // CAMPIELLO_BRICOLA_MDNS_BROWSER_H
