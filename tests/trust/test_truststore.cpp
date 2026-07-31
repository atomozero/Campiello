// test_truststore.cpp
//
// Tests the fingerprint hex/equality helpers, the paired-peer TrustStore (pin, forget, the
// trust decision, and atomic save/load), and the per-node Paths. Portable (no OpenSSL); no
// test framework.

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#include <sys/stat.h>

#include "../../src/traghetto/tls/Fingerprint.h"
#include "../../src/traghetto/trust/Paths.h"
#include "../../src/traghetto/trust/TrustStore.h"

using campiello::net::Fingerprint;
using campiello::net::ToHex;
using campiello::net::FromHex;
using campiello::net::Equals;
using campiello::net::TrustStore;
using campiello::net::TrustDecision;

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

static void TestFingerprintHex()
{
	Fingerprint fp = Fp(0xAB);
	std::string hex = ToHex(fp);
	CHECK(hex.size() == 64);
	CHECK(hex == std::string("abababababababababababababababababababababababababababababababab"));

	Fingerprint back;
	CHECK(FromHex(hex, back));
	CHECK(back == fp);

	// Bad length and non-hex are rejected.
	CHECK(!FromHex("abc", back));
	CHECK(!FromHex(std::string(63, 'a'), back));
	CHECK(!FromHex(std::string(63, 'a') + "g", back));

	// Equals against raw bytes.
	std::vector<uint8_t> bytes(fp.begin(), fp.end());
	CHECK(Equals(bytes, fp));
	bytes[0] ^= 0xFF;
	CHECK(!Equals(bytes, fp));
	CHECK(!Equals(std::vector<uint8_t>(31, 0xAB), fp)); // wrong size
}

static void TestTrustDecisions()
{
	TrustStore store;
	CHECK(store.Count() == 0);

	store.Pin(Fp(0x11), "Ada");
	store.Pin(Fp(0x22), "Berto");
	CHECK(store.Count() == 2);
	CHECK(store.IsTrusted(Fp(0x11)));
	CHECK(!store.IsTrusted(Fp(0x33)));
	CHECK(store.Find(Fp(0x22)) != nullptr);
	CHECK(store.Find(Fp(0x22))->name == "Berto");

	// Re-pinning by fingerprint updates the name, does not add.
	store.Pin(Fp(0x11), "Ada Lovelace");
	CHECK(store.Count() == 2);
	CHECK(store.Find(Fp(0x11))->name == "Ada Lovelace");

	// Trust decisions.
	CHECK(store.Evaluate(Fp(0x11), "Ada Lovelace") == TrustDecision::kTrusted);
	CHECK(store.Evaluate(Fp(0x99), "Nuovo") == TrustDecision::kUnknown);
	// A pinned name presenting a different key: possible impersonation.
	CHECK(store.Evaluate(Fp(0x99), "Berto") == TrustDecision::kKeyChanged);

	// Forget.
	CHECK(store.Forget(Fp(0x11)));
	CHECK(!store.Forget(Fp(0x11))); // already gone
	CHECK(store.Count() == 1);
	CHECK(!store.IsTrusted(Fp(0x11)));
}

static void TestPersistence()
{
	const char* path = "truststore_test.tmp";
	std::remove(path);

	TrustStore store;
	store.Pin(Fp(0x11), "Ada");
	store.Pin(Fp(0x22), "Berto's Laptop"); // a name with a space
	CHECK(store.SaveToFile(path));

	TrustStore loaded;
	CHECK(loaded.LoadFromFile(path));
	CHECK(loaded.Count() == 2);
	CHECK(loaded.IsTrusted(Fp(0x11)));
	CHECK(loaded.Find(Fp(0x22)) != nullptr);
	CHECK(loaded.Find(Fp(0x22))->name == "Berto's Laptop");

	// Malformed lines are skipped, well-formed ones kept.
	{
		std::ofstream out(path, std::ios::trunc);
		out << "not a fingerprint\n";
		out << ToHex(Fp(0x44)) << " Carla\n";
		out << "short\n";
	}
	TrustStore partial;
	CHECK(partial.LoadFromFile(path));
	CHECK(partial.Count() == 1);
	CHECK(partial.Find(Fp(0x44)) != nullptr);
	CHECK(partial.Find(Fp(0x44))->name == "Carla");

	std::remove(path);
	// Loading a missing file fails.
	TrustStore missing;
	CHECK(!missing.LoadFromFile("does_not_exist_truststore.tmp"));
}

static void TestPaths()
{
	std::string dir;
	CHECK(campiello::net::SettingsDir(dir));
	CHECK(!dir.empty());
	struct stat st;
	CHECK(::stat(dir.c_str(), &st) == 0 && S_ISDIR(st.st_mode));

	std::string idPath;
	CHECK(campiello::net::IdentityPath(idPath));
	CHECK(idPath.size() > std::string("identity.pem").size());
	CHECK(idPath.substr(idPath.size() - 12) == "identity.pem");

	std::string trustPath;
	CHECK(campiello::net::TrustStorePath(trustPath));
	CHECK(trustPath.substr(trustPath.size() - 7) == "trusted");
}

int main()
{
	TestFingerprintHex();
	TestTrustDecisions();
	TestPersistence();
	TestPaths();

	std::printf("%d checks, %d failures\n", gChecks, gFailures);
	return gFailures == 0 ? 0 : 1;
}
