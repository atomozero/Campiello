// SftpKnownHosts.h
//
// Trust-on-first-use for SFTP host keys. When SftpBackend connects to a host it reads that
// host's public key (the raw blob from libssh2_session_hostkey) and consults this store: an
// unseen host is pinned on first connect after the user accepts it; a host whose key later
// differs is flagged as a possible man-in-the-middle rather than silently trusted. This mirrors
// the CNP TrustStore (docs/PROPOSAL.md section 9): trust is by key, and a familiar host name
// presenting a different key is treated as a stranger.
//
// The key is stored as its opaque bytes, exactly as the SSH layer produced them, so this file
// makes no assumption about the key type (RSA/Ed25519/ECDSA) and needs no crypto library. Pure
// standard C++ with atomic file persistence; libssh2 feeds it the key bytes at a higher layer.

#ifndef CAMPIELLO_FONDAMENTA_BACKEND_SFTPKNOWNHOSTS_H
#define CAMPIELLO_FONDAMENTA_BACKEND_SFTPKNOWNHOSTS_H

#include <cstdint>
#include <string>
#include <vector>

namespace campiello {
namespace fondamenta {

// A pinned SFTP host and its public-key blob.
struct KnownHost {
	std::string          host; // "host" or "host:port"; no whitespace
	std::vector<uint8_t> key;  // raw SSH host-key blob, opaque to this layer
};

// What to do about a host presenting a key.
enum class HostKeyStatus {
	kTrusted,    // host is pinned with exactly this key: proceed silently
	kUnknown,    // host not seen before: ask the user before pinning
	kKeyChanged, // host is pinned under a DIFFERENT key: surface as possible MITM before trusting
};

class SftpKnownHosts {
public:
	// Add or replace the pinned key for `host`.
	void Pin(const std::string& host, const std::vector<uint8_t>& key);

	// Remove a host. Returns true if it was present.
	bool Forget(const std::string& host);

	bool IsKnown(const std::string& host) const;
	const KnownHost* Find(const std::string& host) const;

	// Decide how to treat a host presenting `key`.
	HostKeyStatus Evaluate(const std::string& host, const std::vector<uint8_t>& key) const;

	size_t Count() const { return fHosts.size(); }
	const std::vector<KnownHost>& Hosts() const { return fHosts; }

	// Persistence. SaveToFile writes atomically (temp file + rename) with mode 0600. The format
	// is one line per host: "<host> <hex key>". LoadFromFile replaces the current contents and
	// skips malformed lines (untrusted-input posture, rule 7).
	bool SaveToFile(const std::string& path) const;
	bool LoadFromFile(const std::string& path);

private:
	std::vector<KnownHost> fHosts;
};

} // namespace fondamenta
} // namespace campiello

#endif // CAMPIELLO_FONDAMENTA_BACKEND_SFTPKNOWNHOSTS_H
