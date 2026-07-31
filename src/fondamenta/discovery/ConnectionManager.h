// ConnectionManager.h
//
// The per-peer connection layer of the discovery filesystem (docs/DISCOVERY_FS.md, commit 2).
// It is the PeerSource the PathRouter browses: PeerNames() comes from a PeerDirectory (the live
// peer endpoints), and BackendFor(name) lazily establishes a native CNP connection to that peer
// and returns a CnpBackend for it, caching it for reuse.
//
// Client-side trust (the thin spot this work fills, docs/DISCOVERY_FS.md): the peer's TLS SPKI
// fingerprint is captured on connect and evaluated against a client TrustStore - pinned on
// first sight (trust on first use), refused if a peer's key changed (possible impersonation).
// The peer independently authenticates us (mutual TLS) and raises its own one-tap allow prompt
// on its side; until it allows us, its share reads back as access-denied.
//
// Decoupled from discovery via PeerDirectory, so it is loopback-testable against a real
// ServerNode with an injected directory (no mDNS, no mount). The Bricola-backed directory is
// wired in the add-on (commit 3). Portable (OpenSSL + sockets), no Haiku.

#ifndef CAMPIELLO_FONDAMENTA_DISCOVERY_CONNECTIONMANAGER_H
#define CAMPIELLO_FONDAMENTA_DISCOVERY_CONNECTIONMANAGER_H

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "../backend/CnpBackend.h"
#include "../../traghetto/tls/Identity.h"
#include "../../traghetto/tls/TlsConnection.h"
#include "../../traghetto/trust/TrustStore.h"
#include "../../traghetto/wire/Handshake.h"
#include "PathRouter.h" // PeerSource

namespace campiello {
namespace fondamenta {

// A reachable peer: its filesystem-facing name and where to connect.
struct PeerEndpoint {
	std::string name;    // friendly instance name (the router's directory entry, no '/')
	std::string address; // numeric IPv4
	uint16_t    port = 0;
};

// The live set of peers to connect to. Bricola-backed in production, faked in tests.
class PeerDirectory {
public:
	virtual ~PeerDirectory() = default;
	virtual std::vector<PeerEndpoint> Endpoints() = 0;
};

class ConnectionManager : public PeerSource {
public:
	ConnectionManager(PeerDirectory& directory, std::string identityPath,
		std::string trustStorePath, std::string nodeName);
	~ConnectionManager() override;

	// Load-or-generate this node's identity, build the client TLS context, load the client
	// trust store. Returns false on failure (see Error()).
	bool Init();

	// PeerSource: the router's root listing and per-peer backends.
	std::vector<std::string> PeerNames() override;
	PeerBackend* BackendFor(const std::string& name) override;

	// Drop all cached connections.
	void CloseAll();

	const char* Error() const { return fError; }

private:
	struct Conn {
		std::unique_ptr<net::TlsConnection> tls;     // owns the socket + SSL; must outlive backend
		std::unique_ptr<CnpBackend>         backend; // references *tls
	};

	// Connect + pin + HELLO to `endpoint`; on success caches it under its name. False otherwise.
	bool Establish(const PeerEndpoint& endpoint);
	bool FindEndpoint(const std::string& name, PeerEndpoint& out);

	PeerDirectory&     fDirectory;
	std::string        fIdentityPath;
	std::string        fTrustStorePath;
	std::string        fNodeName;

	net::Identity      fIdentity;
	net::TlsContext    fContext;
	net::TrustStore    fTrust;
	wire::NodeIdentity fSelfId;

	std::map<std::string, Conn> fConns; // by peer name
	const char*        fError = nullptr;
};

} // namespace fondamenta
} // namespace campiello

#endif // CAMPIELLO_FONDAMENTA_DISCOVERY_CONNECTIONMANAGER_H
