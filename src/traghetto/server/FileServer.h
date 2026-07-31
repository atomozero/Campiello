// FileServer.h
//
// The server-side CnpBackend: a RequestHandler that serves a rooted local directory (the
// shared "Condivisa" folder) over CNP. Read-only by default; writes (WRITE, the namespace
// mutations, and WRITE_ATTRS) are refused with ACCESS_DENIED unless the FileServer is
// constructed writable (PROPOSAL.md section 12: read-only default per peer, widening scope is
// an explicit opt-in). One FileServer per connection (its own open-file table), created by
// FileServerFactory.
//
// Security (docs/PROPOSAL.md rule 7, treat peers as untrusted): every peer-supplied path is
// resolved and required to stay within the shared root, so `..` traversal and symlink escape
// are rejected. Reads resolve the target with realpath; creates resolve the parent directory
// (which must exist within the root) and append a validated leaf, and open with O_NOFOLLOW so
// a symlink leaf cannot redirect the write. RENAME validates both endpoints. Open handles are
// per-connection, bounded, tagged read/write, and closed when the peer disconnects.
//
// Portable: stat/list/read use POSIX, so it builds and its non-attribute behavior tests on
// Linux too. BFS attribute reads are Haiku-only (guarded by __HAIKU__).

#ifndef CAMPIELLO_TRAGHETTO_SERVER_FILESERVER_H
#define CAMPIELLO_TRAGHETTO_SERVER_FILESERVER_H

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>

#include "../dispatch/Dispatch.h"
#include "../wire/Attributes.h"
#include "../wire/Error.h"
#include "../wire/Frame.h"
#include "../wire/Handshake.h"
#include "../wire/Listing.h"

#ifdef __HAIKU__
class BLooper;
class BQuery;
#endif

namespace campiello {
namespace net {

#ifdef __HAIKU__
// Forwards a live BQuery's B_QUERY_UPDATE messages to the FileServer (defined in FileServer.cpp).
class QueryUpdateHandler;
#endif

// Most open files a single peer may hold at once (untrusted-peer fd-exhaustion guard).
static const size_t kMaxOpenHandles = 256;

// Largest attribute value served inline in a STAT/LIST entry; larger ones are omitted.
static const size_t kMaxServedAttrValue = 1u << 20; // 1 MiB

class FileServer : public RequestHandler {
public:
	// `canonicalRoot` must already be an absolute, symlink-resolved path (no trailing
	// slash), as produced by FileServerFactory. `writable` enables the write path; the
	// default is read-only.
	FileServer(std::string canonicalRoot, wire::NodeIdentity nodeId, bool writable = false);
	~FileServer() override;

	wire::Frame Handle(const wire::Frame& request) override;

	// The dispatcher installs the connection's async push channel here; live queries use it to
	// stream QUERY_UPDATE frames. Cleared (null) at teardown before the writer stops.
	void SetFrameSink(FrameSink sink) override;

private:
	// An open file handle: its fd and whether it was opened for writing.
	struct OpenFile {
		int  fd = -1;
		bool writable = false;
	};

	wire::Frame HandleStat(const std::vector<uint8_t>& payload);
	wire::Frame HandleList(const std::vector<uint8_t>& payload);
	wire::Frame HandleOpen(const std::vector<uint8_t>& payload);
	wire::Frame HandleRead(const std::vector<uint8_t>& payload);
	wire::Frame HandleWrite(const std::vector<uint8_t>& payload);
	wire::Frame HandleClose(const std::vector<uint8_t>& payload);
	wire::Frame HandleMkdir(const std::vector<uint8_t>& payload);
	wire::Frame HandleUnlink(const std::vector<uint8_t>& payload);
	wire::Frame HandleRename(const std::vector<uint8_t>& payload);
	wire::Frame HandleTruncate(const std::vector<uint8_t>& payload);
	wire::Frame HandleReadAttrs(const std::vector<uint8_t>& payload);
	wire::Frame HandleWriteAttrs(const std::vector<uint8_t>& payload);
	// Distributed live query (M4): run a Haiku BQuery on the shared root's volume and return the
	// matching entries (within the shared root, capped by a quota). Initial result set only; live
	// updates are a documented follow-up (see docs/VERIFIED.md). QueryClose is a no-op Ok until a
	// server-side live session exists.
	wire::Frame HandleQueryOpen(const std::vector<uint8_t>& payload);
	wire::Frame HandleQueryClose(const std::vector<uint8_t>& payload);

	// Resolve a peer path (rooted, like "/a/b") to a real absolute path within the shared
	// root. The target must exist. On failure sets `err` (kNotFound, kAccessDenied for an
	// escape, kInvalidRequest).
	bool Resolve(const std::string& reqPath, std::string& outReal, wire::ErrorCode& err);
	// Resolve for creation: the PARENT of `reqPath` must exist within the root; `outReal` is
	// parent + "/" + leaf (which need not exist yet). The leaf is validated (not ".", "..",
	// empty, or containing '/'). On failure sets `err`.
	bool ResolveForCreate(const std::string& reqPath, std::string& outReal, wire::ErrorCode& err);
	bool BuildEntry(const std::string& realPath, const std::string& leafName,
		wire::Entry& out);
	void ReadAttrs(const std::string& realPath, wire::AttrSet& out);
	// Write each attribute back with its type code. Haiku-only; a no-op success off Haiku
	// (the portable FS carries no typed attributes). On failure sets `err`.
	bool WriteAttrs(const std::string& realPath, const wire::AttrSet& attrs, wire::ErrorCode& err);

	// Push a frame on the connection's writer, if a sink is installed. Thread-safe: called from the
	// query looper thread and cleared from the reader thread.
	void PushFrame(const wire::Frame& frame);

#ifdef __HAIKU__
	// A live query the peer opened: its BQuery and the handler that receives its updates.
	struct QuerySession {
		BQuery*             query = nullptr;
		QueryUpdateHandler* handler = nullptr;
	};
	// Handle a live B_QUERY_UPDATE: resolve the entry within the shared root and push QUERY_UPDATE.
	void PushQueryUpdate(uint64_t queryId, int32_t opcode, dev_t device, ino_t directory,
		const std::string& name);
	void CloseQuerySession(uint64_t queryId); // stop + free one session
	void CloseAllQuerySessions();              // teardown
#endif

	std::string fRoot;
	wire::NodeIdentity fNodeId;
	bool fWritable;
	std::map<uint64_t, OpenFile> fOpen;
	uint64_t fNextHandle = 1;

	std::mutex fSinkMutex;
	FrameSink  fSink; // async push channel (set by the dispatcher)

#ifdef __HAIKU__
	std::mutex fQueryMutex;
	BLooper*   fQueryLooper = nullptr; // hosts the per-query update handlers; created lazily
	std::map<uint64_t, QuerySession> fQueries;
#endif
};

// Creates a FileServer per connection, all sharing the same canonical root, node identity,
// and writable policy. IsValid() is false if the root did not resolve.
class FileServerFactory : public HandlerFactory {
public:
	FileServerFactory(const std::string& root, wire::NodeIdentity nodeId, bool writable = false);
	bool IsValid() const { return !fRoot.empty(); }
	std::unique_ptr<RequestHandler> Create(const Fingerprint& peer) override;

private:
	std::string fRoot; // canonical, empty if the root did not resolve
	wire::NodeIdentity fNodeId;
	bool fWritable;
};

} // namespace net
} // namespace campiello

#endif // CAMPIELLO_TRAGHETTO_SERVER_FILESERVER_H
