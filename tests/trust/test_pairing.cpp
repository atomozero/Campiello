// test_pairing.cpp
//
// Tests the pairing policy (src/traghetto/trust/Pairing): a trusted peer is admitted
// silently, an unknown or key-changed peer raises the prompt and is pinned + persisted only
// on allow, a denied peer is refused, and the "no identity" all-zero fingerprint is always
// refused. Uses a fake prompt; portable (no OpenSSL, no Haiku), no test framework.

#include <cstdint>
#include <cstdio>
#include <string>

#include "../../src/traghetto/tls/Fingerprint.h"
#include "../../src/traghetto/trust/Pairing.h"
#include "../../src/traghetto/trust/TokenBucket.h"
#include "../../src/traghetto/trust/TrustStore.h"

using campiello::net::Fingerprint;
using campiello::net::Pairing;
using campiello::net::PairingLimits;
using campiello::net::PairingPrompt;
using campiello::net::TokenBucket;
using campiello::net::TrustDecision;
using campiello::net::TrustStore;

static int gChecks = 0;
static int gFailures = 0;

#define CHECK(cond)                                                            \
	do {                                                                       \
		++gChecks;                                                             \
		if (!(cond)) {                                                         \
			++gFailures;                                                       \
			std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);        \
		}                                                                      \
	} while (0)

static Fingerprint Fp(uint8_t fill)
{
	Fingerprint fp;
	fp.fill(fill);
	return fp;
}

// Records what it was asked and answers with a programmed reply.
class FakePrompt : public PairingPrompt {
public:
	bool answer = true;
	int calls = 0;
	std::string lastName;
	Fingerprint lastFp{};
	TrustDecision lastDecision = TrustDecision::kUnknown;

	bool Ask(const std::string& name, const Fingerprint& fp,
		TrustDecision decision) override
	{
		++calls;
		lastName = name;
		lastFp = fp;
		lastDecision = decision;
		return answer;
	}
};

static void TestSilentForTrusted()
{
	TrustStore store;
	FakePrompt prompt;
	Pairing pairing(store, prompt, ""); // no persistence

	// First contact: prompt is raised, allowed, and the peer is pinned.
	prompt.answer = true;
	CHECK(pairing.Admit(Fp(0x11), "Ada"));
	CHECK(prompt.calls == 1);
	CHECK(prompt.lastDecision == TrustDecision::kUnknown);
	CHECK(prompt.lastName == "Ada");
	CHECK(prompt.lastFp == Fp(0x11));
	CHECK(store.IsTrusted(Fp(0x11)));

	// Second contact by the same key: admitted silently, no prompt.
	CHECK(pairing.Admit(Fp(0x11), "Ada"));
	CHECK(prompt.calls == 1); // unchanged
}

static void TestDenyIsNotPinned()
{
	TrustStore store;
	FakePrompt prompt;
	Pairing pairing(store, prompt, "");

	prompt.answer = false;
	CHECK(!pairing.Admit(Fp(0x22), "Berto"));
	CHECK(prompt.calls == 1);
	CHECK(!store.IsTrusted(Fp(0x22)));
	CHECK(store.Count() == 0);
}

static void TestKeyChanged()
{
	TrustStore store;
	FakePrompt prompt;
	Pairing pairing(store, prompt, "");

	// A known name pinned under one key.
	prompt.answer = true;
	CHECK(pairing.Admit(Fp(0x33), "Carla"));
	CHECK(store.Count() == 1);

	// The same name shows up under a different key: possible impersonation. The prompt must
	// be raised with kKeyChanged, not treated as trusted.
	prompt.calls = 0;
	CHECK(pairing.Admit(Fp(0x44), "Carla"));
	CHECK(prompt.calls == 1);
	CHECK(prompt.lastDecision == TrustDecision::kKeyChanged);
	// Allowed, so the new key is now pinned too (both keys trusted).
	CHECK(store.Count() == 2);
	CHECK(store.IsTrusted(Fp(0x33)));
	CHECK(store.IsTrusted(Fp(0x44)));
}

static void TestNoIdentityRefused()
{
	TrustStore store;
	FakePrompt prompt;
	Pairing pairing(store, prompt, "");

	// The all-zero fingerprint carries no identity (e.g. the plain transport). It is refused
	// without ever raising the prompt or touching the store.
	prompt.answer = true;
	CHECK(!pairing.Admit(Fp(0x00), "Nobody"));
	CHECK(prompt.calls == 0);
	CHECK(store.Count() == 0);
}

static void TestPersistAndReload()
{
	const char* path = "pairing_test.tmp";
	std::remove(path);

	{
		TrustStore store;
		FakePrompt prompt;
		prompt.answer = true;
		Pairing pairing(store, prompt, path);
		CHECK(pairing.Admit(Fp(0x55), "Delia"));
	}

	// A fresh store loads the persisted pairing, so the peer is admitted silently with no
	// prompt: pairing survives a restart.
	{
		TrustStore reloaded;
		CHECK(reloaded.LoadFromFile(path));
		CHECK(reloaded.IsTrusted(Fp(0x55)));

		FakePrompt prompt;
		Pairing pairing(reloaded, prompt, path);
		CHECK(pairing.Admit(Fp(0x55), "Delia"));
		CHECK(prompt.calls == 0);
	}

	std::remove(path);
}

static void TestTokenBucketRefillCap()
{
	int64_t nowMs = 0;
	TokenBucket bucket(2, 100, [&nowMs]() { return nowMs; });

	CHECK(bucket.TryTake());  // 2 -> 1
	CHECK(bucket.TryTake());  // 1 -> 0
	CHECK(!bucket.TryTake()); // empty

	// A long idle refills but never exceeds capacity: only 2 tokens come back, not 10.
	nowMs += 1000;
	CHECK(bucket.TryTake());  // 2 -> 1
	CHECK(bucket.TryTake());  // 1 -> 0
	CHECK(!bucket.TryTake()); // capped at capacity
}

static void TestPromptRateLimited()
{
	TrustStore store;
	FakePrompt prompt;
	prompt.answer = false; // deny, so every fresh peer stays unknown and would prompt

	int64_t nowMs = 0;
	PairingLimits limits;
	limits.burst = 3;
	limits.refillMs = 15000;
	Pairing pairing(store, prompt, "", limits, [&nowMs]() { return nowMs; });

	// The first `burst` unknown peers each raise the prompt.
	for (int i = 0; i < 3; i++)
		CHECK(!pairing.Admit(Fp((uint8_t)(0x60 + i)), "Attacker"));
	CHECK(prompt.calls == 3);

	// The clock has not moved: the next attempt is refused WITHOUT raising a prompt.
	CHECK(!pairing.Admit(Fp(0x70), "Attacker"));
	CHECK(prompt.calls == 3);

	// One refill interval later, exactly one more prompt is allowed, then throttled again.
	nowMs += 15000;
	CHECK(!pairing.Admit(Fp(0x71), "Attacker"));
	CHECK(prompt.calls == 4);
	CHECK(!pairing.Admit(Fp(0x72), "Attacker"));
	CHECK(prompt.calls == 4);
}

static void TestTrustedBypassesRateLimit()
{
	TrustStore store;
	store.Pin(Fp(0x80), "Friend"); // already paired from a previous session
	FakePrompt prompt;
	prompt.answer = false;

	int64_t nowMs = 0;
	PairingLimits limits;
	limits.burst = 1;
	limits.refillMs = 1000000; // effectively no refill during the test
	Pairing pairing(store, prompt, "", limits, [&nowMs]() { return nowMs; });

	// Exhaust the bucket with unknown peers.
	CHECK(!pairing.Admit(Fp(0x81), "Stranger")); // spends the one token
	CHECK(!pairing.Admit(Fp(0x82), "Stranger")); // suppressed
	CHECK(prompt.calls == 1);

	// The trusted peer is still admitted silently despite the empty bucket: reconnecting a
	// paired peer is never rate-limited.
	for (int i = 0; i < 5; i++)
		CHECK(pairing.Admit(Fp(0x80), "Friend"));
	CHECK(prompt.calls == 1);
}

int main()
{
	TestSilentForTrusted();
	TestDenyIsNotPinned();
	TestKeyChanged();
	TestNoIdentityRefused();
	TestPersistAndReload();
	TestTokenBucketRefillCap();
	TestPromptRateLimited();
	TestTrustedBypassesRateLimit();

	std::printf("%d checks, %d failures\n", gChecks, gFailures);
	return gFailures == 0 ? 0 : 1;
}
