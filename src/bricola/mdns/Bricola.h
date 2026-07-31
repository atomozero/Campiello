// Bricola.h
//
// The discovery facade: one object that makes this node visible on the LAN and keeps a live
// list of Campiello peers. It owns the multicast socket, the Responder (advertise us), and the
// Browser (find others), and drives them from a single worker thread: announce on startup and
// on a timer, answer inbound service queries, ingest responses, expire stale peers, and
// multicast a goodbye on Stop.
//
// This is the seam the rest of Campiello uses. The resident daemon builds a ServiceInfo from
// the running ServerNode (bound port, node name, identity fingerprint) and calls Start; the
// Deskbar replicant registers as the PeerObserver to show peers appear and disappear.
//
// Threading: the worker thread owns the socket, Responder, and Browser exclusively, so those
// need no locking. PeerObserver callbacks fire on that worker thread; a Haiku UI observer must
// marshal them to its looper (the replicant forwards via a BMessenger). Peers() returns a
// mutex-guarded snapshot, safe to call from any thread.
//
// Portable core: pure standard C++ and POSIX sockets, no Haiku or BeAPI, so it is testable off
// Haiku. Multicast delivery itself is environment-dependent (see docs/VERIFIED.md open item).

#ifndef CAMPIELLO_BRICOLA_MDNS_BRICOLA_H
#define CAMPIELLO_BRICOLA_MDNS_BRICOLA_H

#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "Browser.h"
#include "MdnsSocket.h"
#include "Peer.h"
#include "Responder.h"

namespace campiello {
namespace bricola {
namespace mdns {

class Bricola : private PeerObserver {
public:
	Bricola() = default;
	~Bricola();
	Bricola(const Bricola&) = delete;
	Bricola& operator=(const Bricola&) = delete;

	// Open the socket and start the worker: advertise `self`, browse for peers, and report
	// changes to `observer` (which may be null; peers are still tracked and readable via
	// Peers()). Returns false if the socket cannot be opened (see Error()); leaves us stopped.
	//
	// `interfaceIpv4` picks the multicast interface (see MdnsSocket::Open). Null means resolve
	// automatically: the CAMPIELLO_MDNS_IFACE env override if set (an advanced knob, e.g.
	// "127.0.0.1" to validate two local nodes), else the primary non-loopback interface.
	bool Start(const ServiceInfo& self, PeerObserver* observer,
		const char* interfaceIpv4 = nullptr);

	// Start browse-only: find peers and report them, but do not advertise this node or answer
	// queries. Used by the Deskbar replicant, which only presents peers; the resident daemon is
	// the one that advertises, so there is no second responder for the same node.
	bool StartBrowsing(PeerObserver* observer, const char* interfaceIpv4 = nullptr);

	// Multicast a goodbye, stop the worker, and close the socket. Idempotent; also called by
	// the destructor.
	void Stop();

	bool IsRunning() const { return fThread.joinable(); }

	// A snapshot of the currently known connectable peers. Thread-safe.
	std::vector<Peer> Peers() const;

	const char* Error() const { return fError; }

private:
	// Shared setup for Start / StartBrowsing. `self` is null in browse-only mode.
	bool StartInternal(const ServiceInfo* self, PeerObserver* observer, bool advertise,
		const char* interfaceIpv4);

	void Run();        // worker loop
	void Advertise();  // send a browse query, plus our announce when advertising

	// PeerObserver, invoked on the worker thread by the Browser. Each updates the snapshot and
	// forwards to the user observer.
	void PeerFound(const Peer& peer) override;
	void PeerUpdated(const Peer& peer) override;
	void PeerLost(const Peer& peer) override;

	static int64_t NowMs();

	MdnsSocket                 fSocket;
	std::unique_ptr<Responder> fResponder;
	std::unique_ptr<Browser>   fBrowser;
	PeerObserver*              fUserObserver = nullptr;

	bool               fAdvertise = true;
	std::thread        fThread;
	std::atomic<bool>  fStop{false};
	mutable std::mutex fMutex;                 // guards fSnapshot
	std::map<std::string, Peer> fSnapshot;     // key: instance FQDN
	const char*        fError = nullptr;
};

} // namespace mdns
} // namespace bricola
} // namespace campiello

#endif // CAMPIELLO_BRICOLA_MDNS_BRICOLA_H
