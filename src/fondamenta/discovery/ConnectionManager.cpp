// ConnectionManager.cpp
//
// See ConnectionManager.h.

#include "ConnectionManager.h"

#include <utility>

namespace campiello {
namespace fondamenta {

ConnectionManager::ConnectionManager(PeerDirectory& directory, std::string identityPath,
	std::string trustStorePath, std::string nodeName)
	:
	fDirectory(directory),
	fIdentityPath(std::move(identityPath)),
	fTrustStorePath(std::move(trustStorePath)),
	fNodeName(std::move(nodeName))
{
}

ConnectionManager::~ConnectionManager()
{
	CloseAll();
}

bool ConnectionManager::Init()
{
	if (!net::Identity::LoadOrGenerate(fIdentityPath.c_str(), fIdentity)) {
		fError = "could not load or generate identity";
		return false;
	}
	if (!fContext.Init(fIdentity, /*server=*/false)) {
		fError = "could not build client TLS context";
		return false;
	}
	fTrust.LoadFromFile(fTrustStorePath); // absent is fine (no peers pinned yet)

	net::Fingerprint fp = fIdentity.GetFingerprint();
	fSelfId.fingerprint.assign(fp.begin(), fp.end());
	fSelfId.caps = { wire::kCapBfs };
	fSelfId.node = fNodeName;

	fError = nullptr;
	return true;
}

std::vector<std::string> ConnectionManager::PeerNames()
{
	std::vector<std::string> names;
	for (const PeerEndpoint& ep : fDirectory.Endpoints())
		names.push_back(ep.name);
	return names;
}

bool ConnectionManager::FindEndpoint(const std::string& name, PeerEndpoint& out)
{
	for (const PeerEndpoint& ep : fDirectory.Endpoints()) {
		if (ep.name == name) {
			out = ep;
			return true;
		}
	}
	return false;
}

bool ConnectionManager::Establish(const PeerEndpoint& endpoint)
{
	if (endpoint.address.empty() || endpoint.port == 0) {
		fError = "peer has no address";
		return false;
	}

	// Connect and capture the peer's key (trust on first use, no fingerprint expected yet).
	auto tls = std::make_unique<net::TlsConnection>();
	if (!net::TlsConnection::Connect(fContext, endpoint.address.c_str(), endpoint.port,
			/*expectedPeer=*/nullptr, *tls)) {
		fError = "TLS connect failed";
		return false;
	}

	// Client-side trust decision on the captured fingerprint.
	net::Fingerprint peerFp = tls->PeerFingerprint();
	net::TrustDecision decision = fTrust.Evaluate(peerFp, endpoint.name);
	if (decision == net::TrustDecision::kKeyChanged) {
		fError = "peer key changed (possible impersonation)";
		return false;
	}
	if (decision == net::TrustDecision::kUnknown) {
		fTrust.Pin(peerFp, endpoint.name);
		fTrust.SaveToFile(fTrustStorePath); // best-effort persist
	}

	// CNP handshake. The backend references *tls, which lives in the heap Conn, so it stays
	// valid when the Conn is moved into the map.
	auto backend = std::make_unique<CnpBackend>(*tls);
	wire::NodeIdentity peerOut;
	if (backend->Hello(fSelfId, peerOut) != BackendStatus::kOk) {
		fError = "CNP HELLO failed";
		return false;
	}

	fConns[endpoint.name] = Conn{std::move(tls), std::move(backend)};
	fError = nullptr;
	return true;
}

PeerBackend* ConnectionManager::BackendFor(const std::string& name)
{
	auto it = fConns.find(name);
	if (it != fConns.end())
		return it->second.backend.get();

	PeerEndpoint endpoint;
	if (!FindEndpoint(name, endpoint))
		return nullptr; // not a current peer
	if (!Establish(endpoint))
		return nullptr; // unreachable / refused
	return fConns[name].backend.get();
}

void ConnectionManager::CloseAll()
{
	fConns.clear(); // unique_ptr destructors close the CnpBackend then the TlsConnection
}

} // namespace fondamenta
} // namespace campiello
