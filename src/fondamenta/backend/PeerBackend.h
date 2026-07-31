// PeerBackend.h
//
// Internal abstraction so the interop and native backends present one interface to the
// Fondamenta front ends. Interop backends (SFTP/libssh2, WebDAV, NFS4) implement the read
// subset; the native backend (CnpBackend) implements everything, including typed attributes
// and, later, queries.
//
// This is expressed in the portable wire types (wire::Entry / wire::Stat / wire::AttrSet)
// and a plain status enum, NOT Haiku's status_t / struct stat, so the backends and their
// tests build in CI. The Haiku-specific Fondamenta front end translates wire::Stat into a
// struct stat (and the attribute set into fs_attr writes) at the FUSE / fs_interface
// boundary. The read subset is mandatory; the write and attribute operations default to
// kUnsupported so interop backends can skip them, and the native CnpBackend implements them.
// Distributed queries come later.

#ifndef CAMPIELLO_FONDAMENTA_BACKEND_PEERBACKEND_H
#define CAMPIELLO_FONDAMENTA_BACKEND_PEERBACKEND_H

#include <cstdint>
#include <string>
#include <vector>

#include "../../traghetto/wire/Listing.h"

namespace campiello {
namespace fondamenta {

// Result of a backend operation: kOk, a peer-reported condition (mirroring the CNP
// ErrorCode), or a local failure (transport/protocol).
enum class BackendStatus {
	kOk,
	kNotFound,
	kAccessDenied,
	kNotADirectory,
	kIsADirectory,
	kUnsupported,
	kIoError,
	kBadHandle,
	kInvalidRequest,
	kTransportError, // could not talk to the peer at all
	kProtocolError,  // the peer's reply was malformed or unexpected
};

class PeerBackend {
public:
	virtual ~PeerBackend() = default;

	// Stat one entry. `out.name` is the leaf; `out.stat` and `out.attrs` are filled.
	virtual BackendStatus Stat(const std::string& path, wire::Entry& out) = 0;

	// List a directory. Each entry carries its stat and attribute set.
	virtual BackendStatus ReadDir(const std::string& path,
		std::vector<wire::Entry>& out) = 0;

	// Open a file for reading; returns an opaque handle and the file size.
	virtual BackendStatus Open(const std::string& path, uint64_t& handle,
		uint64_t& size) = 0;

	// Read up to `length` bytes at `offset` from an open handle. A short read means EOF.
	virtual BackendStatus Read(uint64_t handle, uint64_t offset, uint32_t length,
		std::vector<uint8_t>& out) = 0;

	// Close an open handle.
	virtual BackendStatus Close(uint64_t handle) = 0;

	// Write operations. These default to kUnsupported so an interop backend (SFTP/WebDAV/NFS4,
	// read-mostly) need not implement them; the native CnpBackend overrides them all. Whether a
	// write actually succeeds is still the peer's decision (its per-peer read-only default).

	// Open a file for writing, creating and truncating it; returns an opaque handle.
	virtual BackendStatus OpenWrite(const std::string& path, uint64_t& handle)
	{
		(void)path; (void)handle;
		return BackendStatus::kUnsupported;
	}

	// Write `data` at `offset` to a write handle; `written` is how many bytes were stored.
	virtual BackendStatus Write(uint64_t handle, uint64_t offset,
		const std::vector<uint8_t>& data, uint64_t& written)
	{
		(void)handle; (void)offset; (void)data; (void)written;
		return BackendStatus::kUnsupported;
	}

	virtual BackendStatus Mkdir(const std::string& path, uint32_t mode)
	{
		(void)path; (void)mode;
		return BackendStatus::kUnsupported;
	}

	virtual BackendStatus Unlink(const std::string& path)
	{
		(void)path;
		return BackendStatus::kUnsupported;
	}

	virtual BackendStatus Rename(const std::string& from, const std::string& to)
	{
		(void)from; (void)to;
		return BackendStatus::kUnsupported;
	}

	virtual BackendStatus Truncate(const std::string& path, uint64_t size)
	{
		(void)path; (void)size;
		return BackendStatus::kUnsupported;
	}

	// Read a node's attribute set (name, type code, value), without a full Stat.
	virtual BackendStatus ReadAttrs(const std::string& path, wire::AttrSet& out)
	{
		(void)path; (void)out;
		return BackendStatus::kUnsupported;
	}

	// Write (replace) the named attributes on a node, type codes preserved.
	virtual BackendStatus WriteAttrs(const std::string& path, const wire::AttrSet& attrs)
	{
		(void)path; (void)attrs;
		return BackendStatus::kUnsupported;
	}

	// Distributed query (M4): run a Haiku query predicate on the peer's shared root and fill `out`
	// with the matching entries (named by their path relative to the share). Default unsupported;
	// only native CNP peers implement it.
	virtual BackendStatus Query(const std::string& predicate, std::vector<wire::Entry>& out)
	{
		(void)predicate; (void)out;
		return BackendStatus::kUnsupported;
	}
};

} // namespace fondamenta
} // namespace campiello

#endif // CAMPIELLO_FONDAMENTA_BACKEND_PEERBACKEND_H
