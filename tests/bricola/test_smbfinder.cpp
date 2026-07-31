// test_smbfinder.cpp
//
// Unit test for the SMB host finder's pure helpers: subnet derivation, host -> service, and the
// merge/dedup against mDNS-discovered SMB services. The live TCP scan needs a network and is not
// exercised here. Pure standard C++.

#include <cstdio>
#include <string>
#include <vector>

#include "../../src/vicinato/SmbHostFinder.h"

using namespace campiello::vicinato;

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

int main()
{
	// Subnet derivation.
	CHECK(SubnetPrefix("192.168.2.100") == "192.168.2");
	CHECK(SubnetPrefix("10.0.0.5") == "10.0.0");
	CHECK(SubnetPrefix("garbage").empty());
	CHECK(SubnetPrefix("1.2").empty());

	// Host -> service.
	{
		std::vector<NetworkService> svcs = SmbHostsToServices({"192.168.2.104"});
		CHECK(svcs.size() == 1);
		CHECK(svcs[0].kind == ServiceKind::Smb);
		CHECK(svcs[0].browsable);
		CHECK(svcs[0].backend == BackendKind::Smb);
		CHECK(svcs[0].host == "192.168.2.104");
		CHECK(svcs[0].port == 445);
	}

	// Merge: a scanned host not already present is added.
	{
		std::vector<NetworkService> base; // empty (no mDNS SMB)
		std::vector<NetworkService> merged = MergeSmbHosts(base, {"192.168.2.104"});
		CHECK(merged.size() == 1);
		CHECK(merged[0].host == "192.168.2.104");
	}

	// Merge: a scanned host already covered by an mDNS SMB service is NOT duplicated.
	{
		std::vector<NetworkService> base = SmbHostsToServices({"192.168.2.50"}); // pretend mDNS
		std::vector<NetworkService> merged = MergeSmbHosts(base, {"192.168.2.50", "192.168.2.104"});
		// 192.168.2.50 stays once; 192.168.2.104 is added -> 2 total.
		CHECK(merged.size() == 2);
		int count50 = 0;
		for (const NetworkService& s : merged)
			if (s.host == "192.168.2.50")
				++count50;
		CHECK(count50 == 1);
	}

	std::printf("%d checks, %d failures\n", gChecks, gFailures);
	return gFailures == 0 ? 0 : 1;
}
