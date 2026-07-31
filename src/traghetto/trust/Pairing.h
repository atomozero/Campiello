// Pairing.h
//
// The pairing policy: the glue between a connecting peer's identity and the TrustStore. When
// a peer presents a fingerprint (and a claimed name), Pairing consults the store; a trusted
// peer is admitted silently, and an unknown or key-changed peer raises the one-tap "Allow?"
// prompt. On allow, the peer is pinned and the store is persisted; on deny, it is refused.
//
// The prompt UI is the Haiku front end's job (a BAlert), so it sits behind the abstract
// PairingPrompt seam and the portable core here calls it. This keeps the whole policy
// testable with a fake prompt and off any Haiku dependency (docs/PROPOSAL.md sections 9, 12).
//
// Portable (no OpenSSL, no Haiku). Thread-safe: the daemon serves each peer on its own
// thread, so Admit is serialized by a mutex, which also means at most one prompt is raised at
// a time.

#ifndef CAMPIELLO_TRAGHETTO_TRUST_PAIRING_H
#define CAMPIELLO_TRAGHETTO_TRUST_PAIRING_H

#include <mutex>
#include <string>

#include "../tls/Fingerprint.h"
#include "TokenBucket.h"
#include "TrustStore.h"

namespace campiello {
namespace net {

// How hard a hostile peer may be allowed to lean on the prompt: a token bucket of `burst`
// prompts that refills one prompt every `refillMs`. Trusted (already pinned) peers are never
// rate-limited; only the unknown / key-changed path, which raises a prompt, is gated.
struct PairingLimits {
	int burst = 5;             // prompts allowed back to back
	int64_t refillMs = 15000;  // one more prompt every 15s
};

// The user-consent seam. The portable core calls Ask(); the Haiku front end implements it
// with a one-tap BAlert ("Allow NomePC to connect?"). A headless build (a CLI, a test) can
// supply a policy that auto-denies or auto-allows.
class PairingPrompt {
public:
	virtual ~PairingPrompt() = default;

	// Ask the user whether to trust a peer. `decision` is kUnknown (first contact) or
	// kKeyChanged (a pinned name presenting a different key: possible impersonation, to be
	// shown more sternly). `name` is the peer's advertised, attacker-controlled label, shown
	// only as a hint. `fp` is the identity that will actually be pinned. Return true to allow.
	virtual bool Ask(const std::string& name, const Fingerprint& fp,
		TrustDecision decision) = 0;
};

class Pairing {
public:
	// `store` and `prompt` must outlive the Pairing. `storePath` is where an updated store is
	// persisted after a successful pairing (see trust/Paths.h); an empty path disables the
	// persist step (useful in tests). `limits` bounds how often the prompt may be raised;
	// `clock` is the token bucket's time source (injected in tests, steady clock by default).
	Pairing(TrustStore& store, PairingPrompt& prompt, std::string storePath,
			PairingLimits limits = {},
			TokenBucket::ClockMs clock = TokenBucket::SteadyClockMs)
		: fStore(store), fPrompt(prompt), fStorePath(std::move(storePath)),
		  fPromptBucket(limits.burst, limits.refillMs, std::move(clock)) {}

	Pairing(const Pairing&) = delete;
	Pairing& operator=(const Pairing&) = delete;

	// Decide whether to admit a peer that authenticated as `fp` and claims `name`. A trusted
	// fingerprint is admitted silently; otherwise the prompt is raised and, on allow, the peer
	// is pinned and the store persisted. Returns true if the peer may proceed.
	//
	// An all-zero fingerprint (no cryptographic identity, e.g. the plain transport) is always
	// refused. A prompt is raised only if the rate limiter allows it; when the bucket is empty
	// the peer is refused quietly, without a prompt, so a storm of attempts cannot barrage the
	// user (docs/PROPOSAL.md section 12).
	bool Admit(const Fingerprint& fp, const std::string& name);

private:
	std::mutex fMutex;
	TrustStore& fStore;
	PairingPrompt& fPrompt;
	std::string fStorePath;
	TokenBucket fPromptBucket; // guarded by fMutex
};

} // namespace net
} // namespace campiello

#endif // CAMPIELLO_TRAGHETTO_TRUST_PAIRING_H
