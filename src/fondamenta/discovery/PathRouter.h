// PathRouter.h
//
// The routing layer of the discovery filesystem (docs/DISCOVERY_FS.md). It is itself a
// PeerBackend, so it plugs straight into the Fondamenta front end, but it presents two levels:
//   - the root "/" lists the discovered peers (names from an injected PeerSource);
//   - "/<peer>/<rest>" delegates to that peer's own PeerBackend (also from the PeerSource),
//     with the "/<rest>" path, so a peer's share browses like any directory.
//
// The peer entries and the "/<peer>" folders are synthetic read-only directories; everything
// below a peer is served by that peer's backend. Open file handles are namespaced here (each
// peer backend numbers its own handles independently), so the router maps its own handle to a
// (backend, backend-handle) pair. Read-only for now (the write methods inherit PeerBackend's
// kUnsupported default); a peer's share is browsed, not modified through this surface yet.
//
// Pure standard C++ (no Haiku, no network): the routing is unit-testable off Haiku with a fake
// PeerSource and fake backends. The live wiring (Bricola as the peer source, a per-peer
// connection manager as the backend factory) comes in later commits.

#ifndef CAMPIELLO_FONDAMENTA_DISCOVERY_PATHROUTER_H
#define CAMPIELLO_FONDAMENTA_DISCOVERY_PATHROUTER_H

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "../backend/PeerBackend.h"

namespace campiello {
namespace fondamenta {

// Supplies the router with the current peer set and each peer's browsing backend.
class PeerSource {
public:
	virtual ~PeerSource() = default;

	// The peers to show in the root directory (friendly names, filesystem-safe, no '/').
	virtual std::vector<std::string> PeerNames() = 0;

	// A backend for browsing `peerName`, or nullptr if the peer is unknown or currently
	// unreachable. May connect lazily. Ownership stays with the source; the returned backend
	// must outlive the calls the router makes with it.
	virtual PeerBackend* BackendFor(const std::string& peerName) = 0;
};

class PathRouter : public PeerBackend {
public:
	explicit PathRouter(PeerSource& source) : fSource(source) {}

	BackendStatus Stat(const std::string& path, wire::Entry& out) override;
	BackendStatus ReadDir(const std::string& path, std::vector<wire::Entry>& out) override;
	BackendStatus Open(const std::string& path, uint64_t& handle, uint64_t& size) override;
	BackendStatus Read(uint64_t handle, uint64_t offset, uint32_t length,
		std::vector<uint8_t>& out) override;
	BackendStatus Close(uint64_t handle) override;

private:
	struct OpenHandle {
		PeerBackend* backend = nullptr;
		uint64_t     backendHandle = 0;
	};

	// Split a rooted path into the first component (the peer) and the remainder path.
	// "/Studio/a/b" -> peer "Studio", rest "/a/b"; "/Studio" -> peer "Studio", rest "/".
	// Returns false for "/" or an empty path (no peer component).
	bool SplitPeer(const std::string& path, std::string& peer, std::string& rest) const;

	bool PeerExists(const std::string& peer);

	PeerSource&                    fSource;
	std::map<uint64_t, OpenHandle> fOpen;
	uint64_t                       fNextHandle = 1;
};

} // namespace fondamenta
} // namespace campiello

#endif // CAMPIELLO_FONDAMENTA_DISCOVERY_PATHROUTER_H
