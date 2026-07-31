// TrustStore.h
//
// The persistent set of paired ("allowed") peers, keyed by identity fingerprint. This is
// the trust-on-first-use store: pairing pins a peer here, and later connections are accepted
// silently if their fingerprint is pinned. Revoking a peer is removing it. Trust is by key,
// never by the advertised name (docs/PROPOSAL.md section 9), so a familiar name presenting a
// different key reads as a stranger (and, flagged as kKeyChanged, as possible impersonation).
//
// Portable (no OpenSSL): just fingerprints and names, with atomic file persistence. The UI
// (the one-tap "Allow?" prompt) lives in the Haiku front end, not here.

#ifndef CAMPIELLO_TRAGHETTO_TRUST_TRUSTSTORE_H
#define CAMPIELLO_TRAGHETTO_TRUST_TRUSTSTORE_H

#include <cstddef>
#include <string>
#include <vector>

#include "../tls/Fingerprint.h"

namespace campiello {
namespace net {

struct PinnedPeer {
	Fingerprint fingerprint;
	std::string name; // friendly label from pairing; attacker-controllable, not identity
};

// What to do about a connecting peer presenting a fingerprint and a claimed name.
enum class TrustDecision {
	kTrusted,    // fingerprint is pinned: proceed silently
	kUnknown,    // not seen before: raise the pairing prompt
	kKeyChanged, // a peer with this name is pinned under a DIFFERENT key: surface as a
	             // possible impersonation / key rotation before trusting
};

class TrustStore {
public:
	// Add or update (by fingerprint) a trusted peer.
	void Pin(const Fingerprint& fingerprint, const std::string& name);

	// Remove a peer. Returns true if it was present.
	bool Forget(const Fingerprint& fingerprint);

	bool IsTrusted(const Fingerprint& fingerprint) const;
	const PinnedPeer* Find(const Fingerprint& fingerprint) const;

	// Decide how to treat a connecting peer.
	TrustDecision Evaluate(const Fingerprint& fingerprint,
		const std::string& claimedName) const;

	size_t Count() const { return fPeers.size(); }
	const std::vector<PinnedPeer>& Peers() const { return fPeers; }

	// Persistence. SaveToFile writes atomically (temp file + rename) with mode 0600. The
	// format is one line per peer: "<64 hex fingerprint> <name>". LoadFromFile replaces the
	// current contents and skips malformed lines.
	bool SaveToFile(const std::string& path) const;
	bool LoadFromFile(const std::string& path);

private:
	std::vector<PinnedPeer> fPeers;
};

} // namespace net
} // namespace campiello

#endif // CAMPIELLO_TRAGHETTO_TRUST_TRUSTSTORE_H
