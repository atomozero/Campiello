// Fingerprint.h
//
// A node's identity fingerprint: the 32-byte SHA-256 of its DER SubjectPublicKeyInfo. This
// is the value trust pins. Kept in its own small, OpenSSL-free header so the trust store and
// other consumers can use it without pulling in the TLS stack. Computing a fingerprint from
// a key/cert lives in Identity.h (that needs OpenSSL); this header is just the type and its
// hex encoding.

#ifndef CAMPIELLO_TRAGHETTO_TLS_FINGERPRINT_H
#define CAMPIELLO_TRAGHETTO_TLS_FINGERPRINT_H

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace campiello {
namespace net {

using Fingerprint = std::array<uint8_t, 32>;

// Lowercase hex (64 chars). For logs and the mDNS TXT record; never shown to users.
std::string ToHex(const Fingerprint& fp);

// Parse exactly 64 hex chars into a fingerprint. False on wrong length or non-hex.
bool FromHex(const std::string& hex, Fingerprint& out);

// True if a fingerprint carried as raw bytes (e.g. the CNP HELLO/WELCOME `fp` field) equals
// `fp`. Used to cross-check that a peer's claimed CNP identity matches its TLS-pinned key.
bool Equals(const std::vector<uint8_t>& bytes, const Fingerprint& fp);

} // namespace net
} // namespace campiello

#endif // CAMPIELLO_TRAGHETTO_TLS_FINGERPRINT_H
