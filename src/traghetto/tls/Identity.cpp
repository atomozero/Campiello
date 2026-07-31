// Identity.cpp
//
// Implementation of the node identity. See Identity.h.

#include "Identity.h"

#include <cstdio>

#include <sys/stat.h>

#include <openssl/bio.h>
#include <openssl/pem.h>
#include <openssl/sha.h>

namespace campiello {
namespace net {

namespace {

// SHA-256 of the DER SubjectPublicKeyInfo of a public key.
bool FingerprintOfKey(EVP_PKEY* key, Fingerprint& out)
{
	unsigned char* der = nullptr;
	int len = i2d_PUBKEY(key, &der);
	if (len <= 0 || der == nullptr)
		return false;
	unsigned int mdLen = 0;
	int ok = EVP_Digest(der, (size_t)len, out.data(), &mdLen, EVP_sha256(), nullptr);
	OPENSSL_free(der);
	return ok == 1 && mdLen == out.size();
}

// Build a self-signed certificate carrying `key`. Subject/issuer are a fixed placeholder
// CN; identity is the key, not the name, so the CN is meaningless by design.
X509* MakeSelfSigned(EVP_PKEY* key)
{
	X509* cert = X509_new();
	if (cert == nullptr)
		return nullptr;

	X509_set_version(cert, 2); // X.509 v3
	ASN1_INTEGER_set(X509_get_serialNumber(cert), 1);
	X509_gmtime_adj(X509_getm_notBefore(cert), 0);
	X509_gmtime_adj(X509_getm_notAfter(cert), (long)60 * 60 * 24 * 3650); // ~10 years
	X509_set_pubkey(cert, key);

	X509_NAME* name = X509_get_subject_name(cert);
	X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
		(const unsigned char*)"campiello", -1, -1, 0);
	X509_set_issuer_name(cert, name);

	// Ed25519 signs with a NULL digest.
	if (X509_sign(cert, key, nullptr) == 0) {
		X509_free(cert);
		return nullptr;
	}
	return cert;
}

} // namespace

Identity::~Identity()
{
	X509_free(fCert);
	EVP_PKEY_free(fKey);
}

Identity::Identity(Identity&& other) noexcept
	: fKey(other.fKey), fCert(other.fCert)
{
	other.fKey = nullptr;
	other.fCert = nullptr;
}

Identity& Identity::operator=(Identity&& other) noexcept
{
	if (this != &other) {
		X509_free(fCert);
		EVP_PKEY_free(fKey);
		fKey = other.fKey;
		fCert = other.fCert;
		other.fKey = nullptr;
		other.fCert = nullptr;
	}
	return *this;
}

bool Identity::Generate(Identity& out)
{
	EVP_PKEY* key = EVP_PKEY_Q_keygen(nullptr, nullptr, "ED25519");
	if (key == nullptr)
		return false;
	X509* cert = MakeSelfSigned(key);
	if (cert == nullptr) {
		EVP_PKEY_free(key);
		return false;
	}
	out = Identity();
	out.fKey = key;
	out.fCert = cert;
	return true;
}

bool Identity::SaveToFile(const char* path) const
{
	if (!IsValid())
		return false;
	BIO* bio = BIO_new_file(path, "w");
	if (bio == nullptr)
		return false;
	int ok = PEM_write_bio_PrivateKey(bio, fKey, nullptr, nullptr, 0, nullptr, nullptr)
		&& PEM_write_bio_X509(bio, fCert);
	BIO_free(bio);
	if (!ok)
		return false;
	chmod(path, 0600); // key material stays private
	return true;
}

bool Identity::LoadFromFile(const char* path, Identity& out)
{
	BIO* bio = BIO_new_file(path, "r");
	if (bio == nullptr)
		return false;
	EVP_PKEY* key = PEM_read_bio_PrivateKey(bio, nullptr, nullptr, nullptr);
	X509* cert = (key != nullptr) ? PEM_read_bio_X509(bio, nullptr, nullptr, nullptr)
		: nullptr;
	BIO_free(bio);
	if (key == nullptr || cert == nullptr) {
		X509_free(cert);
		EVP_PKEY_free(key);
		return false;
	}
	out = Identity();
	out.fKey = key;
	out.fCert = cert;
	return true;
}

bool Identity::LoadOrGenerate(const char* path, Identity& out)
{
	if (LoadFromFile(path, out))
		return true;
	if (!Generate(out))
		return false;
	return out.SaveToFile(path);
}

Fingerprint Identity::GetFingerprint() const
{
	Fingerprint fp{};
	if (fKey != nullptr)
		FingerprintOfKey(fKey, fp);
	return fp;
}

bool FingerprintOfCert(X509* cert, Fingerprint& out)
{
	if (cert == nullptr)
		return false;
	EVP_PKEY* key = X509_get_pubkey(cert); // bumps the ref count
	if (key == nullptr)
		return false;
	bool ok = FingerprintOfKey(key, out);
	EVP_PKEY_free(key);
	return ok;
}

} // namespace net
} // namespace campiello
