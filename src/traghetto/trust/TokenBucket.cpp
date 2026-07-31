// TokenBucket.cpp
//
// Implementation of the token bucket. See TokenBucket.h.

#include "TokenBucket.h"

#include <chrono>

namespace campiello {
namespace net {

TokenBucket::TokenBucket(int capacity, int64_t refillMs, ClockMs clock)
	: fCapacity(capacity < 1 ? 1 : capacity),
	  fRefillMs(refillMs < 1 ? 1 : refillMs),
	  fClock(std::move(clock)),
	  fTokens(0.0),
	  fLast(0)
{
	fTokens = (double)fCapacity;
	fLast = fClock();
}

bool TokenBucket::TryTake()
{
	int64_t now = fClock();
	int64_t elapsed = now - fLast;
	if (elapsed > 0) {
		double refilled = fTokens + (double)elapsed / (double)fRefillMs;
		double cap = (double)fCapacity;
		fTokens = refilled > cap ? cap : refilled;
		fLast = now;
	}
	if (fTokens >= 1.0) {
		fTokens -= 1.0;
		return true;
	}
	return false;
}

int64_t TokenBucket::SteadyClockMs()
{
	using namespace std::chrono;
	return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

} // namespace net
} // namespace campiello
