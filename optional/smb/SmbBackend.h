// SmbBackend.h
//
// The interop SMB backend: browses a Windows (or Samba) SMB2/3 share as a Fondamenta volume,
// implementing the PeerBackend read subset with libsmb2. The counterpart to SftpBackend, for
// Windows "shared drives" that need no server-side setup (SMB is on by default).
//
// LICENSING: libsmb2 is LGPL-2.1, so this backend lives under optional/ and is built into a
// SEPARATE optional package (dynamically linking libsmb2.so); the MIT core never depends on it
// (working agreement rule 2, docs/SMB.md). Our own code here stays MIT; the LGPL obligation is
// satisfied by dynamic linking.
//
// Implements the PeerBackend read AND write subset over libsmb2 (write/create/mkdir/unlink/rmdir/
// rename/truncate); whether a write succeeds is still the share's decision. SMB has no SSH-style
// host key, so there is no trust-on-first-use here: auth is user/password/domain. Portable (libsmb2
// + POSIX); its integration test runs against a real share.

#ifndef CAMPIELLO_OPTIONAL_SMB_SMBBACKEND_H
#define CAMPIELLO_OPTIONAL_SMB_SMBBACKEND_H

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "../../src/fondamenta/backend/PeerBackend.h"

// Opaque libsmb2 types, so this header does not pull in the LGPL smb2 headers.
struct smb2_context;
struct smb2fh;

namespace campiello {
namespace fondamenta {

struct SmbConfig {
	std::string server;   // hostname or numeric IP
	std::string share;    // share name (e.g. "Users", "Public")
	std::string user;
	std::string password;
	std::string domain;   // optional (Windows domain / workgroup)
	std::string basePath; // optional subdir within the share; request paths are appended
};

class SmbBackend : public PeerBackend {
public:
	SmbBackend() = default;
	~SmbBackend() override;
	SmbBackend(const SmbBackend&) = delete;
	SmbBackend& operator=(const SmbBackend&) = delete;

	// Connect and authenticate to the share. Returns kOk or a failure (kAccessDenied for auth,
	// kTransportError for connect, kNotFound for a missing share).
	BackendStatus Connect(const SmbConfig& config);
	void Disconnect();
	bool IsConnected() const { return fSmb != nullptr; }

	// List the file shares on a host without mounting one: connects to IPC$ with the config's
	// credentials and enumerates via srvsvc (NetrShareEnumAll), so the connect helper can offer a
	// share picker instead of asking the user to type the name. `config.share` is ignored. Fills
	// `shares` with the visible disk shares (hidden admin shares like C$ are excluded). Modern
	// Windows blocks anonymous enumeration, so credentials are normally required. Uses its own
	// short-lived session, independent of Connect/Disconnect.
	BackendStatus EnumShares(const SmbConfig& config, std::vector<std::string>& shares);

	// PeerBackend read subset.
	BackendStatus Stat(const std::string& path, wire::Entry& out) override;
	BackendStatus ReadDir(const std::string& path, std::vector<wire::Entry>& out) override;
	BackendStatus Open(const std::string& path, uint64_t& handle, uint64_t& size) override;
	BackendStatus Read(uint64_t handle, uint64_t offset, uint32_t length,
		std::vector<uint8_t>& out) override;
	BackendStatus Close(uint64_t handle) override;

	// PeerBackend write subset (whether a write actually succeeds is still the server's decision,
	// per the share's permissions). Unlink removes a file or an empty directory.
	BackendStatus OpenWrite(const std::string& path, uint64_t& handle) override;
	BackendStatus Write(uint64_t handle, uint64_t offset, const std::vector<uint8_t>& data,
		uint64_t& written) override;
	BackendStatus Mkdir(const std::string& path, uint32_t mode) override;
	BackendStatus Unlink(const std::string& path) override;
	BackendStatus Rename(const std::string& from, const std::string& to) override;
	BackendStatus Truncate(const std::string& path, uint64_t size) override;

	const char* Error() const { return fError; }

private:
	// Map a peer path ("/a/b") to a share-relative path ("a/b", "" for the root), prefixing the
	// configured base path.
	std::string SharePath(const std::string& reqPath) const;

	smb2_context*                    fSmb = nullptr;
	std::map<uint64_t, smb2fh*>      fOpen;
	uint64_t                         fNextHandle = 1;
	std::string                      fBasePath;
	const char*                      fError = nullptr;
};

} // namespace fondamenta
} // namespace campiello

#endif // CAMPIELLO_OPTIONAL_SMB_SMBBACKEND_H
