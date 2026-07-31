// ServerNode.h
//
// The wired-together server stack: node identity, TLS context, listener, the file backend,
// and the pairing gate, all driving one Daemon. Start() brings the node up on a port and
// Stop() tears it down cleanly. This is the portable core of the resident daemon; the Haiku
// BApplication host (DaemonApp) is a thin shell that resolves paths, supplies a
// HaikuPairingPrompt, and calls Start().
//
// The PairingPrompt is injected, so the whole stack is testable end to end over loopback with
// a fake prompt (a real TLS client pairs and browses the shared folder), off any Haiku or UI
// dependency. Built on OpenSSL (via the TLS layer); portable to Haiku and Linux.

#ifndef CAMPIELLO_TRAGHETTO_SERVER_SERVERNODE_H
#define CAMPIELLO_TRAGHETTO_SERVER_SERVERNODE_H

#include <cstdint>
#include <memory>
#include <string>

#include "../tls/Identity.h"
#include "../tls/TlsConnection.h"
#include "../transport/Connection.h"
#include "../trust/Pairing.h"
#include "../trust/TrustStore.h"
#include "Daemon.h"
#include "FileServer.h"
#include "GatedHandler.h"

namespace campiello {
namespace net {

// The default CNP TCP port. Fixed so a peer can connect before discovery exists; Bricola's
// mDNS advertises the actually-bound port, and ServerConfig::port can override it.
static const uint16_t kDefaultCnpPort = 7735;

struct ServerConfig {
	std::string identityPath;   // PEM key + cert (created on first run if absent)
	std::string trustStorePath; // paired-peer store (may not exist yet)
	std::string sharedRoot;     // the shared folder served to peers; must already exist
	std::string nodeName;       // friendly label announced in HELLO/WELCOME
	std::string bindHost = "0.0.0.0"; // numeric IPv4; 0.0.0.0 = all interfaces
	uint16_t port = kDefaultCnpPort;  // 0 = ephemeral (read back with Port())
	bool writable = false;            // allow the write path; default read-only (PROPOSAL 12)
};

class ServerNode {
public:
	ServerNode() = default;
	~ServerNode();
	ServerNode(const ServerNode&) = delete;
	ServerNode& operator=(const ServerNode&) = delete;

	// Wire everything and start accepting. `prompt` must outlive the node. Returns false on
	// any setup failure (see Error()); the node is left stopped. Not re-startable after Stop.
	bool Start(const ServerConfig& config, PairingPrompt& prompt);

	// Stop accepting and join peer threads. Idempotent; also called by the destructor.
	void Stop();

	bool IsRunning() const { return fDaemon != nullptr; }
	uint16_t Port() const { return fListener.Port(); }        // actual bound port
	const Fingerprint& IdentityFingerprint() const { return fFingerprint; }
	const char* Error() const { return fError; }

private:
	const char* fError = nullptr;
	Identity fIdentity;
	Fingerprint fFingerprint{};
	TrustStore fTrustStore;
	TlsContext fContext;
	Listener fListener;
	std::unique_ptr<FileServerFactory> fFileFactory;
	std::unique_ptr<Pairing> fPairing;
	std::unique_ptr<GatedHandlerFactory> fGatedFactory;
	std::unique_ptr<TlsChannelFactory> fTlsFactory;
	std::unique_ptr<Daemon> fDaemon; // last: destroyed (and thus stopped) before its deps
};

} // namespace net
} // namespace campiello

#endif // CAMPIELLO_TRAGHETTO_SERVER_SERVERNODE_H
