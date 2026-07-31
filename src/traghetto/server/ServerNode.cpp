// ServerNode.cpp
//
// Implementation of the wired server stack. See ServerNode.h.

#include "ServerNode.h"

#include "../wire/Handshake.h"

namespace campiello {
namespace net {

ServerNode::~ServerNode()
{
	Stop();
}

bool ServerNode::Start(const ServerConfig& config, PairingPrompt& prompt)
{
	if (fDaemon) {
		fError = "already running";
		return false;
	}

	// Identity: load the long-lived key + cert, or generate and persist one on first run.
	if (!Identity::LoadOrGenerate(config.identityPath.c_str(), fIdentity)) {
		fError = "identity load or generate failed";
		return false;
	}
	fFingerprint = fIdentity.GetFingerprint();

	// Trust store: best effort. A missing file just means no peers are pinned yet.
	fTrustStore.LoadFromFile(config.trustStorePath);

	if (!fContext.Init(fIdentity, /*server=*/true)) {
		fError = "TLS context init failed";
		return false;
	}

	if (!fListener.Listen(config.bindHost.c_str(), config.port)) {
		fError = "listen failed";
		return false;
	}

	// The identity we announce in HELLO/WELCOME: the pinned key plus the friendly name and
	// capabilities. Trust is by the fingerprint, never the name (docs/PROPOSAL.md section 9).
	wire::NodeIdentity nodeId;
	nodeId.fingerprint.assign(fFingerprint.begin(), fFingerprint.end());
	nodeId.caps = { wire::kCapBfs };
	nodeId.node = config.nodeName;

	fFileFactory.reset(new FileServerFactory(config.sharedRoot, nodeId, config.writable));
	if (!fFileFactory->IsValid()) {
		fError = "shared root did not resolve";
		fFileFactory.reset();
		fListener.Close();
		return false;
	}

	// The pairing gate wraps the file backend: an unknown peer must be admitted (via the
	// prompt) before any request reaches the files.
	fPairing.reset(new Pairing(fTrustStore, prompt, config.trustStorePath));
	fGatedFactory.reset(new GatedHandlerFactory(*fPairing, *fFileFactory));
	fTlsFactory.reset(new TlsChannelFactory(fContext));
	fDaemon.reset(new Daemon(fListener, *fTlsFactory, *fGatedFactory));

	if (!fDaemon->Start()) {
		fError = "daemon start failed";
		Stop();
		return false;
	}

	fError = nullptr;
	return true;
}

void ServerNode::Stop()
{
	if (fDaemon) {
		fDaemon->Stop();
		fDaemon.reset();
	}
	fTlsFactory.reset();
	fGatedFactory.reset();
	fPairing.reset();
	fFileFactory.reset();
	fListener.Close();
}

} // namespace net
} // namespace campiello
