// test_identity.cpp
//
// Tests for the node identity: generation, a 32-byte fingerprint, distinct fingerprints
// per identity, cert-SPKI matching the key, PEM save/load round-trip, and LoadOrGenerate's
// trust-on-first-use behavior. Uses OpenSSL; no test framework.

#include <cstdint>
#include <cstdio>
#include <string>

#include "../../src/traghetto/tls/Identity.h"

using campiello::net::Identity;
using campiello::net::Fingerprint;
using campiello::net::FingerprintOfCert;
using campiello::net::ToHex;

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

static bool IsAllZero(const Fingerprint& fp)
{
	for (uint8_t b : fp)
		if (b != 0)
			return false;
	return true;
}

static const char* kTmp = "test_identity.tmp.pem";

int main()
{
	// Generate a valid identity with a non-zero 32-byte fingerprint.
	Identity a;
	CHECK(Identity::Generate(a));
	CHECK(a.IsValid());
	Fingerprint fpa = a.GetFingerprint();
	CHECK(!IsAllZero(fpa));
	CHECK(ToHex(fpa).size() == 64);

	// The certificate's SPKI fingerprint equals the identity's key fingerprint.
	Fingerprint fromCert{};
	CHECK(FingerprintOfCert(a.Cert(), fromCert));
	CHECK(fromCert == fpa);

	// A second identity is distinct.
	Identity b;
	CHECK(Identity::Generate(b));
	CHECK(b.GetFingerprint() != fpa);

	// PEM save/load round-trip preserves the fingerprint.
	CHECK(a.SaveToFile(kTmp));
	Identity loaded;
	CHECK(Identity::LoadFromFile(kTmp, loaded));
	CHECK(loaded.IsValid());
	CHECK(loaded.GetFingerprint() == fpa);

	// LoadOrGenerate: with the file present it loads the same identity.
	Identity reloaded;
	CHECK(Identity::LoadOrGenerate(kTmp, reloaded));
	CHECK(reloaded.GetFingerprint() == fpa);

	std::remove(kTmp);

	// LoadOrGenerate: with no file it generates and persists a new one, stable on reload.
	const char* fresh = "test_identity.fresh.pem";
	std::remove(fresh);
	Identity gen1;
	CHECK(Identity::LoadOrGenerate(fresh, gen1));
	Fingerprint fpGen = gen1.GetFingerprint();
	Identity gen2;
	CHECK(Identity::LoadOrGenerate(fresh, gen2));
	CHECK(gen2.GetFingerprint() == fpGen);
	std::remove(fresh);

	std::printf("%d checks, %d failures\n", gChecks, gFailures);
	return gFailures == 0 ? 0 : 1;
}
