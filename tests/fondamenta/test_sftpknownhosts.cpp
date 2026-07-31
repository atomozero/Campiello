// test_sftpknownhosts.cpp
//
// Tests the SFTP host-key trust-on-first-use store: pin/evaluate (trusted, unknown,
// key-changed), replace-on-repin, forget, and a save/load round-trip that skips malformed
// lines. Pure standard C++, no framework; non-zero exit on failure.

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#include <unistd.h>

#include "../../src/fondamenta/backend/SftpKnownHosts.h"

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

using Bytes = std::vector<uint8_t>;

static const char* kPath = "sftp_known_hosts_test.tmp";

static void TestEvaluate()
{
	SftpKnownHosts store;
	Bytes keyA = {0x00, 0x11, 0x22, 0x33, 0xAA, 0xFF};
	Bytes keyB = {0x00, 0x11, 0x22, 0x33, 0xAA, 0xEE}; // differs in the last byte

	// Unknown before pinning.
	CHECK(store.Evaluate("nas.local:22", keyA) == HostKeyStatus::kUnknown);
	CHECK(!store.IsKnown("nas.local:22"));

	store.Pin("nas.local:22", keyA);
	CHECK(store.IsKnown("nas.local:22"));
	CHECK(store.Count() == 1);

	// Same key -> trusted; different key for the same host -> key-changed (possible MITM).
	CHECK(store.Evaluate("nas.local:22", keyA) == HostKeyStatus::kTrusted);
	CHECK(store.Evaluate("nas.local:22", keyB) == HostKeyStatus::kKeyChanged);
	// A different host is still unknown.
	CHECK(store.Evaluate("other:22", keyA) == HostKeyStatus::kUnknown);

	// Re-pinning replaces the key (e.g. after the user accepts a rotation), not adds a duplicate.
	store.Pin("nas.local:22", keyB);
	CHECK(store.Count() == 1);
	CHECK(store.Evaluate("nas.local:22", keyB) == HostKeyStatus::kTrusted);
	CHECK(store.Evaluate("nas.local:22", keyA) == HostKeyStatus::kKeyChanged);

	// Forget.
	CHECK(store.Forget("nas.local:22"));
	CHECK(!store.Forget("nas.local:22")); // already gone
	CHECK(store.Count() == 0);
	CHECK(store.Evaluate("nas.local:22", keyB) == HostKeyStatus::kUnknown);
}

static void TestPersistence()
{
	::unlink(kPath);
	SftpKnownHosts store;
	store.Pin("alpha:22", Bytes{0xDE, 0xAD, 0xBE, 0xEF});
	store.Pin("beta.local:2222", Bytes{0x01, 0x02, 0x03});
	CHECK(store.SaveToFile(kPath));

	SftpKnownHosts loaded;
	CHECK(loaded.LoadFromFile(kPath));
	CHECK(loaded.Count() == 2);
	CHECK(loaded.Evaluate("alpha:22", Bytes{0xDE, 0xAD, 0xBE, 0xEF}) == HostKeyStatus::kTrusted);
	CHECK(loaded.Evaluate("beta.local:2222", Bytes{0x01, 0x02, 0x03}) == HostKeyStatus::kTrusted);
	// A wrong key for a loaded host reads as key-changed, proving the bytes round-tripped.
	CHECK(loaded.Evaluate("alpha:22", Bytes{0x00}) == HostKeyStatus::kKeyChanged);

	::unlink(kPath);
}

static void TestMalformedLinesSkipped()
{
	::unlink(kPath);
	{
		std::ofstream out(kPath);
		out << "good:22 deadbeef\n";      // valid
		out << "nohexkey:22 zzzz\n";       // non-hex key -> skip
		out << "oddlen:22 abc\n";          // odd-length hex -> skip
		out << "nospaceonthisline\n";      // no key field -> skip
		out << " leadingspace deadbeef\n"; // empty host token -> skip
		out << "another:22 0011\n";        // valid
	}
	SftpKnownHosts loaded;
	CHECK(loaded.LoadFromFile(kPath));
	CHECK(loaded.Count() == 2);
	CHECK(loaded.IsKnown("good:22"));
	CHECK(loaded.IsKnown("another:22"));
	CHECK(!loaded.IsKnown("nohexkey:22"));
	CHECK(!loaded.IsKnown("oddlen:22"));

	::unlink(kPath);
}

static void TestLoadMissingFile()
{
	::unlink(kPath);
	SftpKnownHosts store;
	CHECK(!store.LoadFromFile(kPath)); // absent file -> false, store stays empty
	CHECK(store.Count() == 0);
}

int main()
{
	TestEvaluate();
	TestPersistence();
	TestMalformedLinesSkipped();
	TestLoadMissingFile();

	std::printf("%d checks, %d failures\n", gChecks, gFailures);
	return gFailures == 0 ? 0 : 1;
}
