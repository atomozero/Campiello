// FileServer.cpp
//
// Implementation of the server-side CnpBackend. See FileServer.h.

#include "FileServer.h"

#include <cerrno>
#include <climits>
#include <cstdlib>
#include <cstring>

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../wire/AttrOps.h"
#include "../wire/FileOps.h"
#include "../wire/Namespace.h"
#include "../wire/Query.h"

#ifdef __HAIKU__
#include <fs_attr.h>
#include <fs_query.h> // B_LIVE_QUERY

#include <AppDefs.h>     // B_QUERY_UPDATE
#include <Entry.h>
#include <Handler.h>
#include <Looper.h>
#include <Messenger.h>
#include <NodeMonitor.h> // B_ENTRY_CREATED / B_ENTRY_REMOVED
#include <Path.h>
#include <Query.h>
#include <Volume.h>
#endif

namespace campiello {
namespace net {

namespace {

// An ERROR reply. The dispatcher sets the request_id.
wire::Frame Error(wire::ErrorCode code)
{
	wire::Frame f;
	f.type = wire::MessageType::kError;
	f.payload = wire::EncodeError(code);
	return f;
}

// Map a POSIX errno from a mutation to a stable CNP error code.
wire::ErrorCode ErrnoToCode(int e)
{
	switch (e) {
		case ENOENT:  return wire::ErrorCode::kNotFound;
		case EACCES:
		case EPERM:
		case EROFS:
		case ELOOP:   return wire::ErrorCode::kAccessDenied; // ELOOP: O_NOFOLLOW hit a symlink leaf
		case ENOTDIR: return wire::ErrorCode::kNotADirectory;
		case EISDIR:  return wire::ErrorCode::kIsADirectory;
		case ENOSPC:
		case EDQUOT:  return wire::ErrorCode::kNoSpace;
		default:      return wire::ErrorCode::kIOError;
	}
}

// Leaf name of a rooted request path; "." for the root itself.
std::string LeafName(std::string path)
{
	while (path.size() > 1 && path.back() == '/')
		path.pop_back();
	if (path == "/")
		return ".";
	size_t slash = path.find_last_of('/');
	return path.substr(slash + 1);
}

// An attribute name is safe to write if it is 1..255 bytes and carries no control bytes (a
// hostile peer must not smuggle names that corrupt the local index, rule 7).
bool ValidAttrName(const std::string& name)
{
	if (name.empty() || name.size() > wire::kMaxAttrNameBytes)
		return false;
	for (unsigned char c : name) {
		if (c < 0x20 || c == 0x7f)
			return false;
	}
	return true;
}

} // namespace

FileServer::FileServer(std::string canonicalRoot, wire::NodeIdentity nodeId, bool writable)
	: fRoot(std::move(canonicalRoot)), fNodeId(std::move(nodeId)), fWritable(writable)
{
}

FileServer::~FileServer()
{
#ifdef __HAIKU__
	CloseAllQuerySessions();
#endif
	for (auto& entry : fOpen)
		::close(entry.second.fd);
}

void FileServer::SetFrameSink(FrameSink sink)
{
	std::lock_guard<std::mutex> lock(fSinkMutex);
	fSink = std::move(sink);
}

void FileServer::PushFrame(const wire::Frame& frame)
{
	std::lock_guard<std::mutex> lock(fSinkMutex);
	if (fSink)
		fSink(frame);
}

wire::Frame FileServer::Handle(const wire::Frame& request)
{
	switch (request.type) {
		case wire::MessageType::kHello:
			return wire::MakeWelcome(fNodeId, 0);
		case wire::MessageType::kStat:
			return HandleStat(request.payload);
		case wire::MessageType::kList:
			return HandleList(request.payload);
		case wire::MessageType::kOpen:
			return HandleOpen(request.payload);
		case wire::MessageType::kRead:
			return HandleRead(request.payload);
		case wire::MessageType::kWrite:
			return HandleWrite(request.payload);
		case wire::MessageType::kClose:
			return HandleClose(request.payload);
		case wire::MessageType::kMkdir:
			return HandleMkdir(request.payload);
		case wire::MessageType::kUnlink:
			return HandleUnlink(request.payload);
		case wire::MessageType::kRename:
			return HandleRename(request.payload);
		case wire::MessageType::kTruncate:
			return HandleTruncate(request.payload);
		case wire::MessageType::kReadAttrs:
			return HandleReadAttrs(request.payload);
		case wire::MessageType::kWriteAttrs:
			return HandleWriteAttrs(request.payload);
		case wire::MessageType::kQueryOpen:
			return HandleQueryOpen(request.payload);
		case wire::MessageType::kQueryClose:
			return HandleQueryClose(request.payload);
		default:
			return Error(wire::ErrorCode::kUnsupported);
	}
}

bool FileServer::Resolve(const std::string& reqPath, std::string& outReal,
	wire::ErrorCode& err)
{
	if (reqPath.empty() || reqPath[0] != '/'
		|| reqPath.find('\0') != std::string::npos) {
		err = wire::ErrorCode::kInvalidRequest;
		return false;
	}

	std::string joined = fRoot + reqPath; // fRoot has no trailing slash; reqPath starts "/"
	char resolved[PATH_MAX];
	if (realpath(joined.c_str(), resolved) == nullptr) {
		err = wire::ErrorCode::kNotFound;
		return false;
	}
	std::string real(resolved);

	// Must be the root itself or strictly under it.
	std::string prefix = (fRoot == "/") ? "/" : fRoot + "/";
	if (real == fRoot
		|| (real.size() >= prefix.size() && real.compare(0, prefix.size(), prefix) == 0)) {
		outReal = std::move(real);
		return true;
	}
	err = wire::ErrorCode::kAccessDenied; // escape attempt
	return false;
}

bool FileServer::ResolveForCreate(const std::string& reqPath, std::string& outReal,
	wire::ErrorCode& err)
{
	if (reqPath.empty() || reqPath[0] != '/'
		|| reqPath.find('\0') != std::string::npos) {
		err = wire::ErrorCode::kInvalidRequest;
		return false;
	}
	// Split into parent path and leaf; the leaf must be a plain, single name.
	std::string trimmed = reqPath;
	while (trimmed.size() > 1 && trimmed.back() == '/')
		trimmed.pop_back();
	size_t slash = trimmed.find_last_of('/');
	std::string parentReq = (slash == 0) ? "/" : trimmed.substr(0, slash);
	std::string leaf = trimmed.substr(slash + 1);
	if (leaf.empty() || leaf == "." || leaf == ".."
		|| leaf.find('/') != std::string::npos) {
		err = wire::ErrorCode::kInvalidRequest;
		return false;
	}

	std::string parentReal;
	if (!Resolve(parentReq, parentReal, err))
		return false;   // parent must exist within the root

	outReal = (parentReal == "/") ? "/" + leaf : parentReal + "/" + leaf;
	return true;
}

bool FileServer::WriteAttrs(const std::string& realPath, const wire::AttrSet& attrs,
	wire::ErrorCode& err)
{
#ifdef __HAIKU__
	int fd = ::open(realPath.c_str(), O_RDONLY);
	if (fd < 0) {
		err = ErrnoToCode(errno);
		return false;
	}
	bool ok = true;
	for (const wire::Attr& a : attrs) {
		ssize_t n = fs_write_attr(fd, a.name.c_str(), a.type, 0, a.value.data(), a.value.size());
		if (n < 0 || (size_t)n != a.value.size()) {
			err = ErrnoToCode(errno);
			ok = false;
			break;
		}
	}
	::close(fd);
	return ok;
#else
	// The portable build has no typed BFS attributes to persist; fidelity is proven natively
	// (docs/VERIFIED.md section 1). Treat the write as an accepted no-op so the handler's
	// decode, policy, and path-guard are exercisable off Haiku.
	(void)realPath;
	(void)attrs;
	(void)err;
	return true;
#endif
}

void FileServer::ReadAttrs(const std::string& realPath, wire::AttrSet& out)
{
#ifdef __HAIKU__
	DIR* dir = fs_open_attr_dir(realPath.c_str());
	if (dir == nullptr)
		return;
	int fd = ::open(realPath.c_str(), O_RDONLY);
	if (fd < 0) {
		fs_close_attr_dir(dir);
		return;
	}
	struct dirent* de;
	while ((de = fs_read_attr_dir(dir)) != nullptr) {
		if (out.size() >= wire::kMaxAttrs)
			break;
		attr_info info;
		if (fs_stat_attr(fd, de->d_name, &info) != 0)
			continue;
		if (info.size < 0 || (size_t)info.size > kMaxServedAttrValue)
			continue; // omit oversized attributes (docs/VERIFIED.md Risk 4)
		wire::Attr a;
		a.name = de->d_name;
		a.type = info.type;
		a.value.resize((size_t)info.size);
		if (info.size > 0) {
			ssize_t n = fs_read_attr(fd, de->d_name, info.type, 0, a.value.data(),
				(size_t)info.size);
			if (n < 0)
				continue;
			a.value.resize((size_t)n);
		}
		out.push_back(std::move(a));
	}
	::close(fd);
	fs_close_attr_dir(dir);
#else
	(void)realPath;
	(void)out;
#endif
}

bool FileServer::BuildEntry(const std::string& realPath, const std::string& leafName,
	wire::Entry& out)
{
	struct stat st;
	if (::lstat(realPath.c_str(), &st) != 0)
		return false;
	out.name = leafName;
	out.stat.mode = st.st_mode;
	out.stat.size = (uint64_t)st.st_size;
	out.stat.mtime = (int64_t)st.st_mtim.tv_sec;
#ifdef __HAIKU__
	out.stat.crtime = (int64_t)st.st_crtim.tv_sec;
#else
	out.stat.crtime = 0;
#endif
	out.stat.inode = (uint64_t)st.st_ino;
	out.attrs.clear();
	ReadAttrs(realPath, out.attrs);
	return true;
}

wire::Frame FileServer::HandleStat(const std::vector<uint8_t>& payload)
{
	std::string path;
	if (!wire::DecodePathRequest(payload, path))
		return Error(wire::ErrorCode::kInvalidRequest);
	std::string real;
	wire::ErrorCode err;
	if (!Resolve(path, real, err))
		return Error(err);
	wire::Entry entry;
	if (!BuildEntry(real, LeafName(path), entry))
		return Error(wire::ErrorCode::kIOError);

	wire::Frame f;
	f.type = wire::MessageType::kStat;
	f.payload = wire::EncodeStatReply(entry);
	return f;
}

wire::Frame FileServer::HandleList(const std::vector<uint8_t>& payload)
{
	std::string path;
	if (!wire::DecodePathRequest(payload, path))
		return Error(wire::ErrorCode::kInvalidRequest);
	std::string real;
	wire::ErrorCode err;
	if (!Resolve(path, real, err))
		return Error(err);

	struct stat st;
	if (::lstat(real.c_str(), &st) != 0)
		return Error(wire::ErrorCode::kIOError);
	if (!S_ISDIR(st.st_mode))
		return Error(wire::ErrorCode::kNotADirectory);

	DIR* dir = ::opendir(real.c_str());
	if (dir == nullptr)
		return Error(wire::ErrorCode::kIOError);

	std::vector<wire::Entry> entries;
	struct dirent* de;
	while ((de = ::readdir(dir)) != nullptr) {
		if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
			continue;
		if (entries.size() >= wire::kMaxEntriesPerReply)
			break;
		wire::Entry entry;
		if (BuildEntry(real + "/" + de->d_name, de->d_name, entry))
			entries.push_back(std::move(entry));
	}
	::closedir(dir);

	wire::Frame f;
	f.type = wire::MessageType::kList;
	f.payload = wire::EncodeListing(entries);
	return f;
}

wire::Frame FileServer::HandleOpen(const std::vector<uint8_t>& payload)
{
	std::string path;
	uint32_t mode = 0;
	if (!wire::DecodeOpenRequest(payload, path, mode))
		return Error(wire::ErrorCode::kInvalidRequest);
	bool wantWrite = (mode & wire::kOpenWrite) != 0;
	if (wantWrite && !fWritable)
		return Error(wire::ErrorCode::kAccessDenied);
	if (fOpen.size() >= kMaxOpenHandles)
		return Error(wire::ErrorCode::kTooManyOpen);

	int fd = -1;
	uint64_t size = 0;
	wire::ErrorCode err;

	if (wantWrite) {
		// Create/truncate. Resolve the parent within the root, then open the leaf with
		// O_NOFOLLOW so a symlink leaf cannot redirect the write outside the shared root.
		std::string real;
		if (!ResolveForCreate(path, real, err))
			return Error(err);
		int flags = O_CREAT | O_TRUNC | O_NOFOLLOW
			| ((mode & wire::kOpenRead) ? O_RDWR : O_WRONLY);
		fd = ::open(real.c_str(), flags, 0644);
		if (fd < 0)
			return Error(ErrnoToCode(errno));
		size = 0; // truncated
	} else {
		std::string real;
		if (!Resolve(path, real, err))
			return Error(err);
		struct stat st;
		if (::lstat(real.c_str(), &st) != 0)
			return Error(wire::ErrorCode::kIOError);
		if (S_ISDIR(st.st_mode))
			return Error(wire::ErrorCode::kIsADirectory);
		fd = ::open(real.c_str(), O_RDONLY);
		if (fd < 0)
			return Error(wire::ErrorCode::kIOError);
		size = (uint64_t)st.st_size;
	}

	uint64_t handle = fNextHandle++;
	fOpen[handle] = OpenFile{fd, wantWrite};

	wire::Frame f;
	f.type = wire::MessageType::kOpen;
	f.payload = wire::EncodeOpenReply(handle, size);
	return f;
}

wire::Frame FileServer::HandleRead(const std::vector<uint8_t>& payload)
{
	uint64_t handle = 0;
	uint64_t offset = 0;
	uint32_t length = 0;
	if (!wire::DecodeReadRequest(payload, handle, offset, length))
		return Error(wire::ErrorCode::kInvalidRequest);
	auto it = fOpen.find(handle);
	if (it == fOpen.end())
		return Error(wire::ErrorCode::kBadHandle);

	std::vector<uint8_t> buffer(length);
	ssize_t n = ::pread(it->second.fd, buffer.data(), length, (off_t)offset);
	if (n < 0)
		return Error(wire::ErrorCode::kIOError);
	buffer.resize((size_t)n);

	wire::Frame f;
	f.type = wire::MessageType::kRead;
	f.payload = wire::EncodeReadReply(buffer);
	return f;
}

wire::Frame FileServer::HandleWrite(const std::vector<uint8_t>& payload)
{
	uint64_t handle = 0;
	uint64_t offset = 0;
	std::vector<uint8_t> data;
	if (!wire::DecodeWriteRequest(payload, handle, offset, data))
		return Error(wire::ErrorCode::kInvalidRequest);
	auto it = fOpen.find(handle);
	if (it == fOpen.end())
		return Error(wire::ErrorCode::kBadHandle);
	if (!it->second.writable)
		return Error(wire::ErrorCode::kAccessDenied);

	ssize_t n = ::pwrite(it->second.fd, data.data(), data.size(), (off_t)offset);
	if (n < 0)
		return Error(ErrnoToCode(errno));

	wire::Frame f;
	f.type = wire::MessageType::kWrite;
	f.payload = wire::EncodeWriteReply((uint64_t)n);
	return f;
}

wire::Frame FileServer::HandleClose(const std::vector<uint8_t>& payload)
{
	uint64_t handle = 0;
	if (!wire::DecodeCloseRequest(payload, handle))
		return Error(wire::ErrorCode::kInvalidRequest);
	auto it = fOpen.find(handle);
	if (it == fOpen.end())
		return Error(wire::ErrorCode::kBadHandle);
	::close(it->second.fd);
	fOpen.erase(it);

	wire::Frame f;
	f.type = wire::MessageType::kClose;
	f.payload = wire::EncodeOk();
	return f;
}

wire::Frame FileServer::HandleMkdir(const std::vector<uint8_t>& payload)
{
	if (!fWritable)
		return Error(wire::ErrorCode::kAccessDenied);
	std::string path;
	uint32_t mode = 0;
	if (!wire::DecodeMkdirRequest(payload, path, mode))
		return Error(wire::ErrorCode::kInvalidRequest);
	std::string real;
	wire::ErrorCode err;
	if (!ResolveForCreate(path, real, err))
		return Error(err);
	if (::mkdir(real.c_str(), mode & 0777) != 0)
		return Error(ErrnoToCode(errno));
	return wire::MakeMkdirReply(0);
}

wire::Frame FileServer::HandleUnlink(const std::vector<uint8_t>& payload)
{
	if (!fWritable)
		return Error(wire::ErrorCode::kAccessDenied);
	std::string path;
	if (!wire::DecodePathRequest(payload, path))
		return Error(wire::ErrorCode::kInvalidRequest);
	// Resolve via the parent so the final component is removed as-is (unlink/rmdir do not
	// follow a final symlink), keeping the operation inside the shared root.
	std::string real;
	wire::ErrorCode err;
	if (!ResolveForCreate(path, real, err))
		return Error(err);
	struct stat st;
	if (::lstat(real.c_str(), &st) != 0)
		return Error(wire::ErrorCode::kNotFound);
	int rc = S_ISDIR(st.st_mode) ? ::rmdir(real.c_str()) : ::unlink(real.c_str());
	if (rc != 0)
		return Error(ErrnoToCode(errno));
	return wire::MakeUnlinkReply(0);
}

wire::Frame FileServer::HandleRename(const std::vector<uint8_t>& payload)
{
	if (!fWritable)
		return Error(wire::ErrorCode::kAccessDenied);
	std::string from, to;
	if (!wire::DecodeRenameRequest(payload, from, to))
		return Error(wire::ErrorCode::kInvalidRequest);
	// Both endpoints must resolve within the shared root (their parents must exist there).
	std::string fromReal, toReal;
	wire::ErrorCode err;
	if (!ResolveForCreate(from, fromReal, err))
		return Error(err);
	if (!ResolveForCreate(to, toReal, err))
		return Error(err);
	if (::rename(fromReal.c_str(), toReal.c_str()) != 0)
		return Error(ErrnoToCode(errno));
	return wire::MakeRenameReply(0);
}

wire::Frame FileServer::HandleTruncate(const std::vector<uint8_t>& payload)
{
	if (!fWritable)
		return Error(wire::ErrorCode::kAccessDenied);
	std::string path;
	uint64_t size = 0;
	if (!wire::DecodeTruncateRequest(payload, path, size))
		return Error(wire::ErrorCode::kInvalidRequest);
	std::string real;
	wire::ErrorCode err;
	if (!Resolve(path, real, err))   // target must exist
		return Error(err);
	if (::truncate(real.c_str(), (off_t)size) != 0)
		return Error(ErrnoToCode(errno));
	return wire::MakeTruncateReply(0);
}

wire::Frame FileServer::HandleReadAttrs(const std::vector<uint8_t>& payload)
{
	std::string path;
	if (!wire::DecodePathRequest(payload, path))
		return Error(wire::ErrorCode::kInvalidRequest);
	std::string real;
	wire::ErrorCode err;
	if (!Resolve(path, real, err))
		return Error(err);
	wire::AttrSet attrs;
	ReadAttrs(real, attrs);
	return wire::MakeReadAttrsReply(attrs, 0);
}

wire::Frame FileServer::HandleWriteAttrs(const std::vector<uint8_t>& payload)
{
	if (!fWritable)
		return Error(wire::ErrorCode::kAccessDenied);
	std::string path;
	wire::AttrSet attrs;
	if (!wire::DecodeWriteAttrsRequest(payload, path, attrs))
		return Error(wire::ErrorCode::kInvalidRequest);
	for (const wire::Attr& a : attrs) {
		if (!ValidAttrName(a.name))
			return Error(wire::ErrorCode::kInvalidRequest);
	}
	std::string real;
	wire::ErrorCode err;
	if (!Resolve(path, real, err))   // target must exist
		return Error(err);
	if (!WriteAttrs(real, attrs, err))
		return Error(err);
	return wire::MakeWriteAttrsReply(0);
}

// Cap on the number of entries returned for one query, so a broad predicate on a busy volume cannot
// flood the peer. Matched to the per-reply entry cap.
static const size_t kQueryResultQuota = 4096;

#ifdef __HAIKU__

// Receives a live BQuery's B_QUERY_UPDATE messages on the query looper thread and forwards the
// notification fields to a callback (which resolves the entry and pushes the QUERY_UPDATE frame).
class QueryUpdateHandler : public BHandler {
public:
	using UpdateFn = std::function<void(int32, dev_t, ino_t, const char*)>;
	explicit QueryUpdateHandler(UpdateFn fn) : BHandler("cnp_query"), fFn(std::move(fn)) {}

	void MessageReceived(BMessage* message) override
	{
		if (message->what != B_QUERY_UPDATE) {
			BHandler::MessageReceived(message);
			return;
		}
		int32 opcode = 0;
		int32 device = 0;
		int64 directory = 0;
		const char* name = nullptr;
		message->FindInt32("opcode", &opcode);
		message->FindInt32("device", &device);
		message->FindInt64("directory", &directory);
		message->FindString("name", &name);
		fFn(opcode, static_cast<dev_t>(device), static_cast<ino_t>(directory), name);
	}

private:
	UpdateFn fFn;
};

#endif // __HAIKU__

wire::Frame FileServer::HandleQueryOpen(const std::vector<uint8_t>& payload)
{
	uint64_t queryId = 0;
	std::string predicate;
	if (!wire::DecodeQueryOpenRequest(payload, queryId, predicate))
		return Error(wire::ErrorCode::kInvalidRequest);

#ifdef __HAIKU__
	// The query runs on the volume that hosts the shared root; results are then filtered to entries
	// that actually live inside the shared root (a BQuery matches across the whole volume, so this
	// boundary check is the security invariant that stops a query from leaking files outside it).
	BEntry rootEntry(fRoot.c_str(), true);
	node_ref rootNode;
	if (rootEntry.InitCheck() != B_OK || rootEntry.GetNodeRef(&rootNode) != B_OK)
		return Error(wire::ErrorCode::kInternal);
	BVolume volume(rootNode.device);
	if (volume.InitCheck() != B_OK || !volume.KnowsQuery())
		return Error(wire::ErrorCode::kUnsupported);

	// A live query needs the connection's async push channel; without one (e.g. FileServer::Handle
	// driven directly in a test) fall back to a one-shot fetch that just returns the current set.
	bool live;
	{
		std::lock_guard<std::mutex> lock(fSinkMutex);
		live = static_cast<bool>(fSink);
	}

	BQuery* query = new BQuery();
	if (query->SetVolume(&volume) != B_OK || query->SetPredicate(predicate.c_str()) != B_OK) {
		delete query;
		return Error(wire::ErrorCode::kInvalidRequest);
	}

	QueryUpdateHandler* handler = nullptr;
	if (live) {
		query->SetFlags(B_LIVE_QUERY);
		handler = new QueryUpdateHandler(
			[this, queryId](int32 opcode, dev_t device, ino_t directory, const char* name) {
				PushQueryUpdate(queryId, opcode, device, directory, name ? name : "");
			});
		if (fQueryLooper == nullptr) {
			fQueryLooper = new BLooper("cnp_query_updates");
			fQueryLooper->Run();
		}
		if (fQueryLooper->Lock()) {
			fQueryLooper->AddHandler(handler);
			fQueryLooper->Unlock();
		}
		query->SetTarget(BMessenger(handler));
	}

	if (query->Fetch() != B_OK) {
		if (handler != nullptr) {
			if (fQueryLooper != nullptr && fQueryLooper->Lock()) {
				fQueryLooper->RemoveHandler(handler);
				fQueryLooper->Unlock();
			}
			delete handler;
		}
		delete query;
		return Error(wire::ErrorCode::kInvalidRequest);
	}

	// Stream the current match set (bounded, within the shared root). Named by path relative to the
	// shared root, so results across subdirectories stay unique and locatable by the client.
	const std::string prefix = fRoot + "/";
	std::vector<wire::Entry> entries;
	entry_ref ref;
	while (entries.size() < kQueryResultQuota && query->GetNextRef(&ref) == B_OK) {
		BPath path(&ref);
		if (path.InitCheck() != B_OK)
			continue;
		std::string real = path.Path();
		if (real.compare(0, prefix.size(), prefix) != 0)
			continue; // outside the shared root: never leak it
		wire::Entry e;
		if (BuildEntry(real, real.substr(prefix.size()), e))
			entries.push_back(std::move(e));
	}

	if (live) {
		CloseQuerySession(queryId); // replace any prior query reusing this id
		std::lock_guard<std::mutex> lock(fQueryMutex);
		fQueries[queryId] = QuerySession{query, handler};
	} else {
		delete query; // one-shot: nothing to keep live
	}
	return wire::MakeQueryResultReply(queryId, entries, true, 0);
#else
	(void)queryId;
	return Error(wire::ErrorCode::kUnsupported);
#endif
}

wire::Frame FileServer::HandleQueryClose(const std::vector<uint8_t>& payload)
{
	uint64_t queryId = 0;
	if (!wire::DecodeQueryCloseRequest(payload, queryId))
		return Error(wire::ErrorCode::kInvalidRequest);
#ifdef __HAIKU__
	CloseQuerySession(queryId);
#endif
	return wire::MakeQueryCloseReply(0);
}

#ifdef __HAIKU__

void FileServer::PushQueryUpdate(uint64_t queryId, int32_t opcode, dev_t device, ino_t directory,
	const std::string& name)
{
	if (name.empty())
		return;
	// Resolve the notification's node ref to a path; skip anything outside the shared root so a
	// live query never leaks entries beyond it (the same boundary invariant as the initial set).
	entry_ref ref(device, directory, name.c_str());
	BPath path(&ref);
	if (path.InitCheck() != B_OK)
		return;
	std::string real = path.Path();
	const std::string prefix = fRoot + "/";
	if (real.compare(0, prefix.size(), prefix) != 0)
		return;
	std::string rel = real.substr(prefix.size());

	bool added = (opcode == B_ENTRY_CREATED);
	wire::Entry entry;
	if (added) {
		if (!BuildEntry(real, rel, entry)) {
			entry = wire::Entry{};
			entry.name = rel;
		}
	} else {
		entry.name = rel; // removed: the file is gone, so path/name only, no stat
	}
	PushFrame(wire::MakeQueryUpdate(queryId, added, entry, 0));
}

void FileServer::CloseQuerySession(uint64_t queryId)
{
	QuerySession session;
	{
		std::lock_guard<std::mutex> lock(fQueryMutex);
		auto it = fQueries.find(queryId);
		if (it == fQueries.end())
			return;
		session = it->second;
		fQueries.erase(it);
	}
	// Stop new updates first (deleting the BQuery drops its live target), then remove and free the
	// handler under the looper lock, which blocks until the looper is not dispatching a message.
	delete session.query;
	if (session.handler != nullptr && fQueryLooper != nullptr) {
		if (fQueryLooper->Lock()) {
			fQueryLooper->RemoveHandler(session.handler);
			fQueryLooper->Unlock();
		}
		delete session.handler;
	}
}

void FileServer::CloseAllQuerySessions()
{
	std::map<uint64_t, QuerySession> sessions;
	{
		std::lock_guard<std::mutex> lock(fQueryMutex);
		sessions.swap(fQueries);
	}
	for (auto& kv : sessions)
		delete kv.second.query; // stop all live updates before touching the looper
	if (fQueryLooper != nullptr) {
		if (fQueryLooper->Lock()) {
			for (auto& kv : sessions) {
				if (kv.second.handler != nullptr) {
					fQueryLooper->RemoveHandler(kv.second.handler);
					delete kv.second.handler;
				}
			}
			fQueryLooper->Quit(); // unlocks, stops the looper thread, and deletes the looper
		}
		fQueryLooper = nullptr;
	}
}

#endif // __HAIKU__

FileServerFactory::FileServerFactory(const std::string& root, wire::NodeIdentity nodeId,
	bool writable)
	: fNodeId(std::move(nodeId)), fWritable(writable)
{
	char resolved[PATH_MAX];
	if (realpath(root.c_str(), resolved) != nullptr)
		fRoot = resolved; // canonical, no trailing slash
}

std::unique_ptr<RequestHandler> FileServerFactory::Create(const Fingerprint&)
{
	if (fRoot.empty())
		return nullptr;
	return std::unique_ptr<RequestHandler>(new FileServer(fRoot, fNodeId, fWritable));
}

} // namespace net
} // namespace campiello
