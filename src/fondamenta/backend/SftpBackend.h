// SftpBackend.h
//
// The interop (SFTP) backend: implements the PeerBackend read subset over libssh2's SFTP
// subsystem, so a remote SSH/SFTP host can be browsed and read as a Fondamenta volume (M1).
// The write methods inherit PeerBackend's kUnsupported default: M1 is read-only.
//
// Trust: on connect the host's public key is checked against an SftpKnownHosts store (host-key
// trust-on-first-use, mirroring the CNP model). An unknown or changed key is referred to an
// injected decision callback (the connect helper prompts the user; tests auto-accept); only on
// acceptance is the key pinned and the session allowed to proceed.
//
// libssh2 is BSD-3 (core-legal, docs/VERIFIED.md section 7). Signatures verified against the
// installed libssh2 1.11.1 headers. Portable (libssh2 is cross-platform); its integration test
// runs against a real SFTP server, so it does not run in a serverless CI.

#ifndef CAMPIELLO_FONDAMENTA_BACKEND_SFTPBACKEND_H
#define CAMPIELLO_FONDAMENTA_BACKEND_SFTPBACKEND_H

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "PeerBackend.h"
#include "SftpKnownHosts.h"

// Opaque libssh2 types, so this header does not pull in libssh2.h.
struct _LIBSSH2_SESSION;
struct _LIBSSH2_SFTP;
struct _LIBSSH2_SFTP_HANDLE;

namespace campiello {
namespace fondamenta {

struct SftpConfig {
	std::string host;                // hostname or numeric IP
	uint16_t    port = 22;
	std::string user;
	std::string password;            // password auth (used when non-empty)
	std::string privateKeyPath;      // public-key auth (used when password is empty)
	std::string publicKeyPath;       // optional companion to privateKeyPath
	std::string keyPassphrase;       // optional
	std::string basePath;            // remote base dir; request paths are appended to it
};

class SftpBackend : public PeerBackend {
public:
	// Decide whether to trust a host whose key is unknown or has changed. Given the status and
	// the observed key bytes, return true to pin and proceed, false to abort the connection.
	using HostKeyDecision =
		std::function<bool(HostKeyStatus, const std::vector<uint8_t>&)>;

	SftpBackend() = default;
	~SftpBackend() override;
	SftpBackend(const SftpBackend&) = delete;
	SftpBackend& operator=(const SftpBackend&) = delete;

	// Connect, verify the host key against `knownHosts` (persisted to `knownHostsPath` when a
	// new key is pinned), authenticate, and start the SFTP subsystem. Returns kOk or a failure
	// (kTransportError for connect/handshake, kAccessDenied for a rejected key or failed auth).
	BackendStatus Connect(const SftpConfig& config, SftpKnownHosts& knownHosts,
		const std::string& knownHostsPath, const HostKeyDecision& decision);

	void Disconnect();
	bool IsConnected() const { return fSftp != nullptr; }

	// PeerBackend read subset.
	BackendStatus Stat(const std::string& path, wire::Entry& out) override;
	BackendStatus ReadDir(const std::string& path, std::vector<wire::Entry>& out) override;
	BackendStatus Open(const std::string& path, uint64_t& handle, uint64_t& size) override;
	BackendStatus Read(uint64_t handle, uint64_t offset, uint32_t length,
		std::vector<uint8_t>& out) override;
	BackendStatus Close(uint64_t handle) override;

	// Human-readable reason for the last failure, developer log only.
	const char* Error() const { return fError; }

private:
	// Map a peer path ("/a/b") to the remote path (basePath + path).
	std::string RemotePath(const std::string& reqPath) const;
	// Translate the SFTP subsystem's last error into a BackendStatus.
	BackendStatus SftpError() const;
	// True if the session's last error was a dead socket (dropped/idle-timed-out connection),
	// as opposed to an ordinary SFTP error like not-found.
	bool SessionDead() const;
	// Tear down and re-establish the session from the stored config, re-verifying the (already
	// pinned) host key. Open file handles do not survive. Returns true on success.
	bool Reconnect();
	// If the last failure was a dead socket, try one reconnect. True if the caller may retry.
	bool ReconnectIfDead();

	int                 fSocket = -1;
	_LIBSSH2_SESSION*   fSession = nullptr;
	_LIBSSH2_SFTP*      fSftp = nullptr;
	std::map<uint64_t, _LIBSSH2_SFTP_HANDLE*> fOpen;
	uint64_t            fNextHandle = 1;
	std::string         fBasePath;
	const char*         fError = nullptr;

	// Kept so a dropped connection can be transparently re-established (M1 reconnect).
	SftpConfig          fConfig;
	std::string         fKnownHostsPath;
};

} // namespace fondamenta
} // namespace campiello

#endif // CAMPIELLO_FONDAMENTA_BACKEND_SFTPBACKEND_H
