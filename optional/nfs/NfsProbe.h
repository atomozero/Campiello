// NfsProbe.h
//
// Read-only probe for an NFS server discovered via _nfs._tcp: it enumerates the server's exports the
// way "showmount -e" does, over ONC RPC (RFC 1057) on UDP:
//   1. portmapper (program 100000 v2, UDP port 111), PMAPPROC_GETPORT (3): find the mount daemon's
//      port for MOUNT program 100005 v3 over UDP.
//   2. MOUNT (program 100005 v3), MOUNTPROC3_EXPORT (5): return the export list (dir paths).
//
// This does NOT implement an NFS filesystem client (that is a heavy follow-up, docs/addons/nfs.md);
// it only lists exports so the add-on can show the user what to mount. Hand-rolled RPC over plain UDP
// sockets: no third-party dependency (MIT-clean), links only libbe + the network kit.
//
// References: RFC 1057 (ONC RPC), RFC 1833 (Binding Protocols / portmapper), RFC 1813 (NFSv3, which
// includes the MOUNT v3 protocol and MOUNTPROC3_EXPORT).

#ifndef CAMPIELLO_NFS_NFSPROBE_H
#define CAMPIELLO_NFS_NFSPROBE_H

#include <string>
#include <vector>

namespace campiello {
namespace nfs {

class NfsProbe {
public:
	explicit NfsProbe(const std::string& host) : fHost(host) {}

	// The UDP port of the MOUNT daemon (0 on failure).
	int MountPort();

	// The server's exported directory paths (empty on failure). `okOut` reports whether the mount
	// daemon answered.
	std::vector<std::string> ListExports(bool* okOut);

private:
	bool UdpRpc(int port, const std::string& request, std::string* reply);

	std::string fHost;
};

// RPC message helpers (dependency-free; exposed for testing).
namespace rpc {
	void PutU32(std::string& b, uint32_t v);
	void PutString(std::string& b, const std::string& s); // length-prefixed, padded to 4 bytes
	uint32_t GetU32(const std::string& b, size_t* off);
	std::string GetString(const std::string& b, size_t* off);
	// Build a CALL message (AUTH_NULL cred/verf, no args unless appended by the caller).
	std::string BuildCall(uint32_t xid, uint32_t prog, uint32_t vers, uint32_t proc);
	// Skip a REPLY header; returns false if the reply is not an accepted success. On success `off`
	// points at the results.
	bool ParseReplyHeader(const std::string& b, size_t* off);
	// Parse a MOUNTPROC3_EXPORT result (export list) into dir paths, starting at `off`.
	std::vector<std::string> ParseExportList(const std::string& b, size_t off);
}

} // namespace nfs
} // namespace campiello

#endif // CAMPIELLO_NFS_NFSPROBE_H
