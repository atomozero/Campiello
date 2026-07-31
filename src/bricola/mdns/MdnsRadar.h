// MdnsRadar.h
//
// A passive mDNS/DNS-SD network monitor: opens the Bricola multicast socket on a chosen
// interface, asks the LAN to enumerate its services (the DNS-SD meta-query), and folds every
// datagram it hears into a live snapshot of who is transmitting and what services exist. It is
// the engine behind the campiello_radar debug window (docs/RADAR.md).
//
// Unlike Bricola/PeerTable, which reconcile ONE service type into connectable peers, the radar
// keeps everything it sees, unfiltered, for diagnosis. It answers "is any multicast reaching us,
// and on which interface?" (docs/VERIFIED.md multicast open items).
//
// Portable: pure standard C++ + POSIX sockets (MdnsSocket) + the MdnsWire codec, no Haiku or
// BeAPI, so the aggregation is unit-testable off Haiku by feeding it packets. A worker thread
// owns the socket and updates a mutex-guarded model that Snapshot() copies out.

#ifndef CAMPIELLO_BRICOLA_MDNS_MDNSRADAR_H
#define CAMPIELLO_BRICOLA_MDNS_MDNSRADAR_H

#include <atomic>
#include <cstdint>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "MdnsSocket.h"

namespace campiello {
namespace bricola {
namespace mdns {

// A source that has sent us at least one datagram.
struct RadarSource {
	std::string ip;
	uint64_t    packets = 0;
	uint64_t    records = 0;
	int64_t     firstMs = 0;
	int64_t     lastMs  = 0;
};

// A DNS-SD service type seen advertised (e.g. "_smb._tcp.local"). DNS-SD subtypes
// ("_printer._sub._http._tcp.local") are folded into their parent service and listed here.
struct RadarService {
	std::string              type;
	size_t                   instances = 0; // filled in the snapshot
	std::vector<std::string> subtypes;      // subtype labels seen, e.g. "_printer"
	int64_t                  lastMs    = 0;
};

// One advertised service instance, accumulated from its PTR/SRV/TXT/A records.
struct RadarInstance {
	std::string                                       name;    // instance FQDN
	std::string                                       type;    // service type
	std::string                                       host;    // SRV target
	uint16_t                                          port = 0;
	std::vector<std::string>                          addrs;   // resolved in the snapshot
	std::vector<std::pair<std::string, std::string>>  txt;
	std::string                                       srcIp;   // sender (address fallback)
	int64_t                                           lastMs = 0;
};

// A consistent copy of the model at one instant.
struct RadarSnapshot {
	std::string                interfaceIp; // "" means INADDR_ANY
	int64_t                    startMs = 0;
	int64_t                    nowMs   = 0;
	uint64_t                   totalPackets   = 0;
	uint64_t                   totalRecords   = 0;
	uint64_t                   droppedPackets = 0; // received but did not parse
	std::vector<RadarSource>   sources;
	std::vector<RadarService>  services;
	std::vector<RadarInstance> instances;
};

class MdnsRadar {
public:
	MdnsRadar() = default;
	~MdnsRadar();
	MdnsRadar(const MdnsRadar&) = delete;
	MdnsRadar& operator=(const MdnsRadar&) = delete;

	// Open the socket on `interfaceIpv4` (null/empty means resolve automatically: the
	// CAMPIELLO_MDNS_IFACE env override, else PrimaryMulticastIPv4) and start the worker.
	// Returns false if the socket cannot open (see Error()).
	bool Start(const char* interfaceIpv4 = nullptr);

	// Stop the worker and close the socket. Idempotent; also called by the destructor.
	void Stop();

	bool IsRunning() const { return fThread.joinable(); }

	// A consistent snapshot of everything seen so far. Thread-safe.
	RadarSnapshot Snapshot() const;

	// Force the service queries to go out now (also sent periodically by the worker).
	void QueryNow() { fQueryNow.store(true); }

	// The interface actually bound after Start (numeric IPv4, "" for INADDR_ANY).
	std::string Interface() const;

	const char* Error() const { return fError; }

	// Fold one raw datagram into the model, as if received from `srcIp` at monotonic `nowMs`.
	// Public so the aggregation can be unit-tested without a socket.
	void Ingest(const uint8_t* buf, size_t len, const std::string& srcIp, int64_t nowMs);

private:
	// Internal accumulation state (guarded by fMutex).
	struct SourceState { RadarSource s; };
	struct InstanceState { RadarInstance i; };

	void Run();               // worker loop
	void SendQueries();       // meta-query + a PTR query per known service type
	static int64_t NowMs();

	MdnsSocket        fSocket;
	std::string       fInterface;
	std::thread       fThread;
	std::atomic<bool> fStop{false};
	std::atomic<bool> fQueryNow{false};
	const char*       fError = nullptr;

	mutable std::mutex fMutex;
	int64_t  fStartMs = 0;
	int64_t  fLastMs  = 0;
	uint64_t fTotalPackets   = 0;
	uint64_t fTotalRecords   = 0;
	uint64_t fDroppedPackets = 0;
	std::map<std::string, RadarSource>    fSources;   // key: ip
	std::map<std::string, RadarService>   fServices;  // key: service type
	std::map<std::string, RadarInstance>  fInstances; // key: instance FQDN
	std::map<std::string, std::set<std::string>> fHostAddrs; // host (lowercased) -> IPv4 set
};

} // namespace mdns
} // namespace bricola
} // namespace campiello

#endif // CAMPIELLO_BRICOLA_MDNS_MDNSRADAR_H
