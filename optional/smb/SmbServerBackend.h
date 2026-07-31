// SmbServerBackend.h
//
// Presents a whole SMB server as one browsable volume, Windows "\\server" style: the volume root
// lists the server's shares as directories, and navigating into a share connects to it lazily and
// browses its files. This is the difference from SmbBackend, which mounts a single named share.
//
// It composes the tested share-level SmbBackend: one per share, connected on first access and
// cached, so all the real SMB I/O stays in SmbBackend. A path is routed by its first component:
//   "/"                       -> the server root (lists shares)
//   "/Public"                 -> the share "Public" (its root directory)
//   "/Public/docs/report.txt" -> path "docs/report.txt" inside share "Public"
// Open handles are remapped to a flat namespace so they never collide across shares.
//
// LICENSING: like SmbBackend, this lives under optional/ (it pulls in libsmb2 transitively) and
// ships only in the separate optional package; the MIT core never depends on it (docs/SMB.md).

#ifndef CAMPIELLO_OPTIONAL_SMB_SMBSERVERBACKEND_H
#define CAMPIELLO_OPTIONAL_SMB_SMBSERVERBACKEND_H

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "SmbBackend.h"

namespace campiello {
namespace fondamenta {

// Split a server-volume path into its first component (the share name) and the remainder within
// that share (with a leading '/', or empty for the share root):
//   "" or "/"       -> ("", "")
//   "/Public"       -> ("Public", "")
//   "/Public/a/b"   -> ("Public", "/a/b")
// Pure and Haiku-free, so it is unit-tested off Haiku.
void SplitServerPath(const std::string& path, std::string& share, std::string& rest);

class SmbServerBackend : public PeerBackend {
public:
	SmbServerBackend() = default;
	~SmbServerBackend() override;
	SmbServerBackend(const SmbServerBackend&) = delete;
	SmbServerBackend& operator=(const SmbServerBackend&) = delete;

	// Connect to the server (config.share is ignored) and enumerate its shares once, so the root
	// listing is ready and the credentials are validated up front. Returns the EnumShares result.
	BackendStatus ConnectServer(const SmbConfig& config);
	void Disconnect();

	// PeerBackend read subset, routed per share.
	BackendStatus Stat(const std::string& path, wire::Entry& out) override;
	BackendStatus ReadDir(const std::string& path, std::vector<wire::Entry>& out) override;
	BackendStatus Open(const std::string& path, uint64_t& handle, uint64_t& size) override;
	BackendStatus Read(uint64_t handle, uint64_t offset, uint32_t length,
		std::vector<uint8_t>& out) override;
	BackendStatus Close(uint64_t handle) override;

	// PeerBackend write subset, routed per share. The server root and a bare share are directories
	// you cannot create/write/delete through here; a rename must stay within one share.
	BackendStatus OpenWrite(const std::string& path, uint64_t& handle) override;
	BackendStatus Write(uint64_t handle, uint64_t offset, const std::vector<uint8_t>& data,
		uint64_t& written) override;
	BackendStatus Mkdir(const std::string& path, uint32_t mode) override;
	BackendStatus Unlink(const std::string& path) override;
	BackendStatus Rename(const std::string& from, const std::string& to) override;
	BackendStatus Truncate(const std::string& path, uint64_t size) override;

	const char* Error() const { return fError; }

private:
	// Lazily connect (and cache) the share-level backend for `share`; null on failure (fError set).
	SmbBackend* ShareBackend(const std::string& share);
	// A synthetic directory entry (for the root and for each share), no network stat needed.
	static wire::Entry DirEntry(const std::string& name);

	SmbConfig                                             fServer;   // share/basePath cleared
	std::vector<std::string>                              fShares;   // enumerated share names
	std::map<std::string, std::unique_ptr<SmbBackend>>    fBackends; // per share, lazy
	std::map<uint64_t, std::pair<SmbBackend*, uint64_t>>  fHandles;  // global handle -> (backend, local)
	uint64_t                                              fNextHandle = 1;
	const char*                                           fError = nullptr;
};

} // namespace fondamenta
} // namespace campiello

#endif // CAMPIELLO_OPTIONAL_SMB_SMBSERVERBACKEND_H
