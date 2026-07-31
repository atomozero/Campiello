// Identity.h
//
// A node's long-lived cryptographic identity for native-mode TLS: an Ed25519 keypair plus
// a self-signed certificate that carries the public key. There is no CA; trust is by
// pinning the key's fingerprint (SHA-256 of the DER SubjectPublicKeyInfo, i2d_PUBKEY),
// which is the stable identity the pairing flow pins (docs/PROTOCOL.md, docs/VERIFIED.md
// section 6). The user never sees any of this.
//
// Built on OpenSSL 3 (Apache-2.0, the M0 decision). Portable (OpenSSL is on Haiku and
// Linux), so this and its test build in CI.

#ifndef CAMPIELLO_TRAGHETTO_TLS_IDENTITY_H
#define CAMPIELLO_TRAGHETTO_TLS_IDENTITY_H

#include <cstdint>
#include <string>

#include <openssl/evp.h>
#include <openssl/x509.h>

#include "Fingerprint.h"

namespace campiello {
namespace net {

class Identity {
public:
	Identity() = default;
	~Identity();
	Identity(Identity&& other) noexcept;
	Identity& operator=(Identity&& other) noexcept;
	Identity(const Identity&) = delete;
	Identity& operator=(const Identity&) = delete;

	// Fresh Ed25519 keypair + self-signed certificate.
	static bool Generate(Identity& out);

	// Load a private key + certificate from a PEM file.
	static bool LoadFromFile(const char* path, Identity& out);

	// Trust-on-first-use setup: load the identity if the file exists, otherwise generate a
	// new one and save it (0600). This is how a node gets its identity on install/first run.
	static bool LoadOrGenerate(const char* path, Identity& out);

	// Write the key + certificate to a PEM file, chmod 0600.
	bool SaveToFile(const char* path) const;

	bool IsValid() const { return fKey != nullptr && fCert != nullptr; }
	Fingerprint GetFingerprint() const;

	// Raw OpenSSL handles for building an SSL_CTX. Not owned by the caller.
	EVP_PKEY* Key() const { return fKey; }
	X509* Cert() const { return fCert; }

private:
	EVP_PKEY* fKey = nullptr;
	X509* fCert = nullptr;
};

// SHA-256 of a certificate's SubjectPublicKeyInfo, used to pin a connected peer.
bool FingerprintOfCert(X509* cert, Fingerprint& out);

} // namespace net
} // namespace campiello

#endif // CAMPIELLO_TRAGHETTO_TLS_IDENTITY_H
