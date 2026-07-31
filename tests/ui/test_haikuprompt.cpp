// test_haikuprompt.cpp
//
// Build + fail-safe check for HaikuPairingPrompt against the real Haiku headers and libbe.
//
// It does NOT pop the actual consent dialog: that needs a running BApplication and a human.
// Instead it runs with no BApplication (be_app == NULL) and asserts Ask() fails safe by
// denying, without ever touching the app_server. Linking also verifies the BAlert / be_app /
// B_ESCAPE usage resolves against libbe (the class vtable pulls Ask, which references them).

#include <cstdio>

#include "../../src/traghetto/server/HaikuPairingPrompt.h"

int main()
{
#ifdef __HAIKU__
	campiello::net::HaikuPairingPrompt prompt;
	campiello::net::Fingerprint fp{};

	// No BApplication was created, so be_app is NULL and Ask() must deny without popping a
	// dialog. The real allow/deny path needs a BApplication and user input, so it is out of
	// scope for an automated test.
	bool allowedUnknown =
		prompt.Ask("Test Peer", fp, campiello::net::TrustDecision::kUnknown);
	bool allowedKeyChanged =
		prompt.Ask("Test Peer", fp, campiello::net::TrustDecision::kKeyChanged);

	if (allowedUnknown || allowedKeyChanged) {
		std::printf("FAIL: Ask() allowed a peer with no BApplication running\n");
		return 1;
	}
	std::printf("ok: no BApplication -> deny; compiled and linked against libbe\n");
#else
	std::printf("skipped (not Haiku)\n");
#endif
	return 0;
}
