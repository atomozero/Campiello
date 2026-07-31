// TokenBucket.h
//
// A monotonic token bucket: `capacity` tokens, one refilled every `refillMs`, starting full.
// TryTake() consumes a token if one is available. Used to bound how often the pairing prompt
// may be raised, so a hostile peer cannot storm it (docs/PROPOSAL.md section 12).
//
// The clock is injected (monotonic milliseconds) so the behavior is deterministically
// testable without sleeping; it defaults to a steady clock. Not thread-safe on its own:
// callers serialize access (Pairing holds its mutex around the check).
//
// Portable (no OpenSSL, no Haiku), standard C++.

#ifndef CAMPIELLO_TRAGHETTO_TRUST_TOKENBUCKET_H
#define CAMPIELLO_TRAGHETTO_TRUST_TOKENBUCKET_H

#include <cstdint>
#include <functional>

namespace campiello {
namespace net {

class TokenBucket {
public:
	// Returns monotonic (non-decreasing) milliseconds; the value's origin is arbitrary.
	using ClockMs = std::function<int64_t()>;

	// `capacity` and `refillMs` are clamped to at least 1. The bucket starts full.
	TokenBucket(int capacity, int64_t refillMs, ClockMs clock = SteadyClockMs);

	// Refill for the elapsed time, then consume one token if available. True if taken.
	bool TryTake();

	// Monotonic milliseconds from std::chrono::steady_clock.
	static int64_t SteadyClockMs();

private:
	int fCapacity;
	int64_t fRefillMs;
	ClockMs fClock;
	double fTokens; // fractional, in [0, fCapacity]
	int64_t fLast;  // timestamp of the last refill (ms)
};

} // namespace net
} // namespace campiello

#endif // CAMPIELLO_TRAGHETTO_TRUST_TOKENBUCKET_H
