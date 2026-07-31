// test_smbserver.cpp
//
// Unit test for SplitServerPath, the pure path router of the server-level ("\\server" style) SMB
// mount. The networked parts of SmbServerBackend need a real server and are exercised by the
// integration test; this covers the routing logic, which is where the bugs would hide.

#include <cstdio>
#include <string>

#include "SmbServerBackend.h"

using namespace campiello::fondamenta;

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

static void Split(const std::string& path, std::string& share, std::string& rest)
{
	SplitServerPath(path, share, rest);
}

int main()
{
	std::string share, rest;

	// Root, in its several spellings, has no share and no remainder.
	Split("", share, rest);
	CHECK(share.empty() && rest.empty());
	Split("/", share, rest);
	CHECK(share.empty() && rest.empty());
	Split("//", share, rest);
	CHECK(share.empty() && rest.empty());

	// A bare share: name captured, no remainder (the share root).
	Split("/Public", share, rest);
	CHECK(share == "Public" && rest.empty());

	// A path inside a share: the remainder keeps its leading slash for SmbBackend.
	Split("/Public/docs/report.txt", share, rest);
	CHECK(share == "Public" && rest == "/docs/report.txt");

	// A share name with spaces (the real "din esp8266 mini" case) is one component.
	Split("/din esp8266 mini", share, rest);
	CHECK(share == "din esp8266 mini" && rest.empty());
	Split("/din esp8266 mini/DIN_Rail_Hook_v2.skp", share, rest);
	CHECK(share == "din esp8266 mini" && rest == "/DIN_Rail_Hook_v2.skp");

	// A trailing slash after a share is an empty (root) remainder, not a phantom subpath.
	Split("/Public/", share, rest);
	CHECK(share == "Public" && rest == "/");

	std::printf("%d checks, %d failures\n", gChecks, gFailures);
	return gFailures == 0 ? 0 : 1;
}
