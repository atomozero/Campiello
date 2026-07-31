// test_gatedhandler.cpp
//
// Tests the server-side pairing gate (src/traghetto/server/GatedHandler): the first message
// must be HELLO, the HELLO's claimed fingerprint must match the TLS-authenticated peer, an
// unknown peer is admitted only if the prompt allows, a denied or not-yet-paired peer is
// refused, and a pinned peer is admitted silently. A fake inner backend records forwarding.
// Portable (no OpenSSL, no sockets): the gate is exercised directly frame by frame.

#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>

#include "../../src/traghetto/server/GatedHandler.h"
#include "../../src/traghetto/tls/Fingerprint.h"
#include "../../src/traghetto/trust/Pairing.h"
#include "../../src/traghetto/trust/TrustStore.h"
#include "../../src/traghetto/wire/Error.h"
#include "../../src/traghetto/wire/Frame.h"
#include "../../src/traghetto/wire/Handshake.h"

namespace wire = campiello::wire;

using campiello::net::Fingerprint;
using campiello::net::GatedHandler;
using campiello::net::Pairing;
using campiello::net::PairingPrompt;
using campiello::net::RequestHandler;
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

// Inner backend stand-in: WELCOME on HELLO, a distinctive kNotFound otherwise (so a forwarded
// non-HELLO request is told apart from the gate's own kAccessDenied), and counts both.
class StubBackend : public RequestHandler {
public:
	int helloCount = 0;
	int otherCount = 0;

	wire::Frame Handle(const wire::Frame& req) override
	{
		if (req.type == wire::MessageType::kHello) {
			++helloCount;
			return wire::MakeWelcome(wire::NodeIdentity{}, 0);
		}
		++otherCount;
		return wire::MakeError(wire::ErrorCode::kNotFound, "", 0);
	}
};

class FakePrompt : public PairingPrompt {
public:
	bool answer = true;
	int calls = 0;
	TrustDecision lastDecision = TrustDecision::kUnknown;

	bool Ask(const std::string&, const Fingerprint&, TrustDecision decision) override
	{
		++calls;
		lastDecision = decision;
		return answer;
	}
};

static wire::Frame Hello(const Fingerprint& fp, const std::string& name)
{
	wire::NodeIdentity id;
	id.fingerprint.assign(fp.begin(), fp.end());
	id.node = name;
	return wire::MakeHello(id, 0);
}

static wire::Frame Bare(wire::MessageType type)
{
	wire::Frame f;
	f.type = type;
	return f;
}

static bool IsError(const wire::Frame& f, wire::ErrorCode code)
{
	if (f.type != wire::MessageType::kError)
		return false;
	wire::ErrorReply er;
	return wire::DecodeError(f.payload, er) && er.code == (uint32_t)code;
}

// A gate plus everything it needs, wired together for one test.
struct Harness {
	TrustStore store;
	FakePrompt prompt;
	Pairing pairing;
	StubBackend* inner; // owned by the gate
	GatedHandler gate;

	Harness(const Fingerprint& peer, StubBackend* backend)
		: pairing(store, prompt, ""),
		  inner(backend),
		  gate(peer, pairing, std::unique_ptr<RequestHandler>(backend)) {}
};

static void TestNonHelloFirstRefused()
{
	Harness h(Fp(0x11), new StubBackend());
	h.prompt.answer = true;

	wire::Frame reply = h.gate.Handle(Bare(wire::MessageType::kStat));
	CHECK(IsError(reply, wire::ErrorCode::kAccessDenied));
	CHECK(h.inner->otherCount == 0); // never reached the backend
	CHECK(h.prompt.calls == 0);      // no prompt for a peer that skipped HELLO
}

static void TestFingerprintMismatchRefused()
{
	Harness h(Fp(0x11), new StubBackend());
	h.prompt.answer = true;

	// HELLO claims a different key than TLS authenticated: impersonation attempt.
	wire::Frame reply = h.gate.Handle(Hello(Fp(0x22), "Ada"));
	CHECK(IsError(reply, wire::ErrorCode::kAccessDenied));
	CHECK(h.inner->helloCount == 0);
	CHECK(h.prompt.calls == 0); // rejected before ever asking the user
}

static void TestMalformedHelloRefused()
{
	Harness h(Fp(0x11), new StubBackend());

	wire::Frame f = Bare(wire::MessageType::kHello);
	f.payload = {0xFF, 0xFF, 0xFF}; // not a valid NodeIdentity body
	wire::Frame reply = h.gate.Handle(f);
	CHECK(IsError(reply, wire::ErrorCode::kInvalidRequest));
	CHECK(h.inner->helloCount == 0);
}

static void TestAllowAdmitsAndForwards()
{
	Harness h(Fp(0x11), new StubBackend());
	h.prompt.answer = true;

	// First contact: prompt allowed, HELLO forwarded (WELCOME), and the peer is pinned.
	wire::Frame reply = h.gate.Handle(Hello(Fp(0x11), "Ada"));
	CHECK(reply.type == wire::MessageType::kWelcome);
	CHECK(h.prompt.calls == 1);
	CHECK(h.prompt.lastDecision == TrustDecision::kUnknown);
	CHECK(h.inner->helloCount == 1);
	CHECK(h.store.IsTrusted(Fp(0x11)));

	// Now admitted: a later request passes straight through to the backend.
	wire::Frame stat = h.gate.Handle(Bare(wire::MessageType::kStat));
	CHECK(IsError(stat, wire::ErrorCode::kNotFound)); // the backend's reply, not the gate's
	CHECK(h.inner->otherCount == 1);
}

static void TestDenyRefusesUntilPaired()
{
	Harness h(Fp(0x33), new StubBackend());
	h.prompt.answer = false;

	wire::Frame reply = h.gate.Handle(Hello(Fp(0x33), "Berto"));
	CHECK(IsError(reply, wire::ErrorCode::kAccessDenied));
	CHECK(h.prompt.calls == 1);
	CHECK(h.inner->helloCount == 0);
	CHECK(!h.store.IsTrusted(Fp(0x33)));

	// Still not admitted: subsequent requests remain refused, backend never reached.
	wire::Frame stat = h.gate.Handle(Bare(wire::MessageType::kStat));
	CHECK(IsError(stat, wire::ErrorCode::kAccessDenied));
	CHECK(h.inner->otherCount == 0);
}

static void TestTrustedIsSilent()
{
	StubBackend* backend = new StubBackend();
	Harness h(Fp(0x44), backend);
	h.store.Pin(Fp(0x44), "Carla"); // already paired from a previous session
	h.prompt.answer = false;        // would deny, but must not be asked

	wire::Frame reply = h.gate.Handle(Hello(Fp(0x44), "Carla"));
	CHECK(reply.type == wire::MessageType::kWelcome);
	CHECK(h.prompt.calls == 0); // silent for a pinned peer
	CHECK(h.inner->helloCount == 1);
}

int main()
{
	TestNonHelloFirstRefused();
	TestFingerprintMismatchRefused();
	TestMalformedHelloRefused();
	TestAllowAdmitsAndForwards();
	TestDenyRefusesUntilPaired();
	TestTrustedIsSilent();

	std::printf("%d checks, %d failures\n", gChecks, gFailures);
	return gFailures == 0 ? 0 : 1;
}
