// Pairing.cpp
//
// Implementation of the pairing policy. See Pairing.h.

#include "Pairing.h"

namespace campiello {
namespace net {

bool Pairing::Admit(const Fingerprint& fp, const std::string& name)
{
	// An all-zero fingerprint is the "no identity" sentinel (e.g. the plain transport). It is
	// never a real SPKI hash, and admitting it would let an unauthenticated peer pair, so
	// refuse before touching the store or the prompt.
	static const Fingerprint kNone{};
	if (fp == kNone)
		return false;

	std::lock_guard<std::mutex> lock(fMutex);

	TrustDecision decision = fStore.Evaluate(fp, name);
	if (decision == TrustDecision::kTrusted)
		return true;

	// Unknown or key-changed: the user decides. Rate-limit first so a hostile peer that
	// reconnects in a loop cannot storm the prompt; over budget, refuse quietly (no prompt).
	if (!fPromptBucket.TryTake())
		return false;

	// Raise the prompt.
	if (!fPrompt.Ask(name, fp, decision))
		return false;

	// Allowed: pin by key (never by name) and persist so the next connection is silent.
	fStore.Pin(fp, name);
	if (!fStorePath.empty())
		fStore.SaveToFile(fStorePath);
	return true;
}

} // namespace net
} // namespace campiello
