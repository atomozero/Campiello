// HaikuPairingPrompt.h
//
// The Haiku implementation of the PairingPrompt seam (see trust/Pairing.h): the one-tap
// "Allow?" consent shown to the user when an unknown or key-changed peer tries to connect. It
// is a modal BAlert, so it is Haiku-only (guarded by __HAIKU__) and requires a BApplication
// (be_app) to be running in the process; the resident daemon/replicant owns that. Without a
// BApplication, consent cannot be obtained and the peer is denied (fail safe).
//
// End-user text is Italian (project rule). The peer's key is never shown (experience rule):
// the friendly name is only a hint, and is sanitized because it is attacker-controlled.

#ifndef CAMPIELLO_TRAGHETTO_SERVER_HAIKUPAIRINGPROMPT_H
#define CAMPIELLO_TRAGHETTO_SERVER_HAIKUPAIRINGPROMPT_H

#ifdef __HAIKU__

#include "../trust/Pairing.h"

namespace campiello {
namespace net {

class HaikuPairingPrompt : public PairingPrompt {
public:
	// Show the modal consent on the calling thread; return true only if the user allowed.
	// Serialized by Pairing's mutex, so at most one dialog is up at a time. Returns false
	// (deny) if no BApplication is running, since BAlert needs an app_server connection.
	bool Ask(const std::string& name, const Fingerprint& fp,
		TrustDecision decision) override;
};

} // namespace net
} // namespace campiello

#endif // __HAIKU__

#endif // CAMPIELLO_TRAGHETTO_SERVER_HAIKUPAIRINGPROMPT_H
