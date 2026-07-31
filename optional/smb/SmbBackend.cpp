// SmbBackend.cpp
//
// Implementation of the interop SMB backend. See SmbBackend.h. Signatures verified against the
// installed libsmb2 headers (smb2/libsmb2.h, smb2/smb2.h).

#include "SmbBackend.h"

#include <algorithm>
#include <csignal>
#include <cstring>

#include <fcntl.h>
#include <poll.h>
#include <sys/stat.h>

#include <smb2/smb2.h>
#include <smb2/libsmb2.h>
#include <smb2/libsmb2-raw.h>
#include <smb2/libsmb2-dcerpc.h>
#include <smb2/libsmb2-dcerpc-srvsvc.h>

namespace campiello {
namespace fondamenta {

namespace {

std::string LeafName(std::string path)
{
	while (path.size() > 1 && path.back() == '/')
		path.pop_back();
	if (path == "/" || path.empty())
		return ".";
	size_t slash = path.find_last_of('/');
	return (slash == std::string::npos) ? path : path.substr(slash + 1);
}

// Map a POSIX errno (positive) from libsmb2 to a BackendStatus.
BackendStatus ErrnoToStatus(int e)
{
	switch (e) {
		case ENOENT:  return BackendStatus::kNotFound;
		case EACCES:
		case EPERM:   return BackendStatus::kAccessDenied;
		case ENOTDIR: return BackendStatus::kNotADirectory;
		case EISDIR:  return BackendStatus::kIsADirectory;
		default:      return BackendStatus::kIoError;
	}
}

void FillWireStat(const struct smb2_stat_64& st, wire::Stat& s)
{
	s = wire::Stat{};
	if (st.smb2_type == SMB2_TYPE_DIRECTORY)
		s.mode = S_IFDIR | 0555;
	else if (st.smb2_type == SMB2_TYPE_LINK)
		s.mode = S_IFLNK | 0777;
	else
		s.mode = S_IFREG | 0444;
	s.size = st.smb2_size;
	s.mtime = static_cast<int64_t>(st.smb2_mtime);
	s.crtime = static_cast<int64_t>(st.smb2_btime); // SMB carries a real birth time
	s.inode = st.smb2_ino;
}

// State handed to the srvsvc share-enumeration callback.
struct ShareEnumState {
	bool                     done = false;
	int                      status = 0;
	std::vector<std::string> shares;
};

void ShareEnumCb(struct smb2_context* smb2, int status, void* commandData, void* cbData)
{
	auto* state = static_cast<ShareEnumState*>(cbData);
	state->done = true;
	state->status = status;
	if (status != 0 || commandData == nullptr)
		return;
	auto* rep = static_cast<struct srvsvc_netshareenumall_rep*>(commandData);
	if (rep->ctr != nullptr && rep->ctr->level == 1) {
		for (uint32_t i = 0; i < rep->ctr->ctr1.count; ++i) {
			const srvsvc_netshareinfo1& info = rep->ctr->ctr1.array[i];
			// Only real disk shares, and not the hidden admin ones (C$, ADMIN$...).
			if ((info.type & 3) != SHARE_TYPE_DISKTREE)
				continue;
			if (info.type & SHARE_TYPE_HIDDEN)
				continue;
			if (info.name != nullptr)
				state->shares.emplace_back(info.name);
		}
	}
	smb2_free_data(smb2, rep);
}

} // namespace

SmbBackend::~SmbBackend()
{
	Disconnect();
}

std::string SmbBackend::SharePath(const std::string& reqPath) const
{
	// libsmb2 paths are relative to the share root ("" is the root), '/'-separated.
	std::string rel = reqPath;
	if (!rel.empty() && rel[0] == '/')
		rel.erase(rel.begin());
	if (fBasePath.empty())
		return rel;
	if (rel.empty())
		return fBasePath;
	return fBasePath + "/" + rel;
}

BackendStatus SmbBackend::Connect(const SmbConfig& config)
{
	if (IsConnected()) {
		fError = "already connected";
		return BackendStatus::kInvalidRequest;
	}
	// A dropped SMB connection must not kill the process with SIGPIPE (as with the SFTP add-on).
	std::signal(SIGPIPE, SIG_IGN);

	fSmb = smb2_init_context();
	if (fSmb == nullptr) {
		fError = "smb2 context init failed";
		return BackendStatus::kTransportError;
	}
	// Bound libsmb2's synchronous operations so a stuck peer cannot hang the mount forever.
	smb2_set_timeout(fSmb, 15);
	if (!config.password.empty())
		smb2_set_password(fSmb, config.password.c_str());
	if (!config.domain.empty())
		smb2_set_domain(fSmb, config.domain.c_str());

	// libsmb2 (HaikuPorts REVISION 3) uses Haiku's NATIVE errno, so it returns 0 on success and
	// -errno (a positive value, since errno is negative here) on failure. Test != 0, and -rc is
	// the native errno our (non-B_USE_POSITIVE_POSIX_ERRORS) constants match. See docs/SMB.md.
	int rc = smb2_connect_share(fSmb, config.server.c_str(), config.share.c_str(),
		config.user.empty() ? nullptr : config.user.c_str());
	if (rc != 0) {
		fError = smb2_get_error(fSmb);
		BackendStatus status;
		int e = -rc;
		if (e == EACCES || e == EPERM)
			status = BackendStatus::kAccessDenied; // bad credentials
		else if (e == ENOENT)
			status = BackendStatus::kNotFound;     // no such share
		else
			status = BackendStatus::kTransportError;
		smb2_destroy_context(fSmb);
		fSmb = nullptr;
		return status;
	}

	// Normalize the base path to a share-relative form (no leading/trailing slash).
	fBasePath = config.basePath;
	while (!fBasePath.empty() && fBasePath.front() == '/')
		fBasePath.erase(fBasePath.begin());
	while (!fBasePath.empty() && fBasePath.back() == '/')
		fBasePath.pop_back();

	fError = nullptr;
	return BackendStatus::kOk;
}

void SmbBackend::Disconnect()
{
	if (fSmb == nullptr)
		return;
	for (auto& entry : fOpen)
		smb2_close(fSmb, entry.second);
	fOpen.clear();
	smb2_disconnect_share(fSmb);
	smb2_destroy_context(fSmb);
	fSmb = nullptr;
}

BackendStatus SmbBackend::EnumShares(const SmbConfig& config, std::vector<std::string>& shares)
{
	shares.clear();

	struct smb2_context* smb2 = smb2_init_context();
	if (smb2 == nullptr) {
		fError = "smb2 context init failed";
		return BackendStatus::kTransportError;
	}
	smb2_set_timeout(smb2, 15);
	if (!config.password.empty())
		smb2_set_password(smb2, config.password.c_str());
	if (!config.domain.empty())
		smb2_set_domain(smb2, config.domain.c_str());

	// Share enumeration runs over the IPC$ pipe.
	int rc = smb2_connect_share(smb2, config.server.c_str(), "IPC$",
		config.user.empty() ? nullptr : config.user.c_str());
	if (rc != 0) {
		fError = smb2_get_error(smb2);
		int e = -rc;
		BackendStatus status = (e == EACCES || e == EPERM) ? BackendStatus::kAccessDenied
			: BackendStatus::kTransportError;
		smb2_destroy_context(smb2);
		return status;
	}

	ShareEnumState state;
	if (smb2_share_enum_async(smb2, ShareEnumCb, &state) < 0) {
		fError = smb2_get_error(smb2);
		smb2_disconnect_share(smb2);
		smb2_destroy_context(smb2);
		return BackendStatus::kIoError;
	}

	// Drive libsmb2's async loop until the callback fires (bounded).
	for (int i = 0; i < 200 && !state.done; ++i) {
		struct pollfd pfd;
		pfd.fd = smb2_get_fd(smb2);
		pfd.events = static_cast<short>(smb2_which_events(smb2));
		if (poll(&pfd, 1, 100) < 0)
			break;
		if (smb2_service(smb2, pfd.revents) < 0) {
			fError = smb2_get_error(smb2);
			break;
		}
	}

	BackendStatus result;
	if (!state.done) {
		fError = "share enumeration timed out";
		result = BackendStatus::kTransportError;
	} else if (state.status != 0) {
		fError = smb2_get_error(smb2);
		result = BackendStatus::kAccessDenied; // usually a permission/anonymous refusal
	} else {
		shares = std::move(state.shares);
		fError = nullptr;
		result = BackendStatus::kOk;
	}

	smb2_disconnect_share(smb2);
	smb2_destroy_context(smb2);
	return result;
}

BackendStatus SmbBackend::Stat(const std::string& path, wire::Entry& out)
{
	if (!IsConnected())
		return BackendStatus::kTransportError;
	std::string sp = SharePath(path);
	if (sp.empty()) {
		// The share root. Present it as a directory WITHOUT a network stat: userlandfs mounts by
		// first issuing getattr("/"), and some SMB servers reject smb2_stat("") on the share root,
		// which would abort the whole mount. A share root is always a directory. (Verified: with a
		// real connection the mount's first call is getattr("/") -> Stat("") -> SharePath("")=="".)
		out.name = LeafName(path);
		out.attrs.clear();
		out.stat = wire::Stat{};
		out.stat.mode = S_IFDIR | 0555;
		return BackendStatus::kOk;
	}
	struct smb2_stat_64 st;
	std::memset(&st, 0, sizeof(st));
	int rc = smb2_stat(fSmb, sp.c_str(), &st);
	if (rc != 0)
		return ErrnoToStatus(-rc);
	out.name = LeafName(path);
	out.attrs.clear();
	FillWireStat(st, out.stat);
	return BackendStatus::kOk;
}

BackendStatus SmbBackend::ReadDir(const std::string& path, std::vector<wire::Entry>& out)
{
	if (!IsConnected())
		return BackendStatus::kTransportError;
	std::string sp = SharePath(path);
	struct smb2dir* dir = smb2_opendir(fSmb, sp.c_str());
	if (dir == nullptr)
		return BackendStatus::kNotFound; // missing directory or not a directory

	out.clear();
	struct smb2dirent* ent;
	while ((ent = smb2_readdir(fSmb, dir)) != nullptr) {
		std::string leaf = ent->name;
		if (leaf == "." || leaf == "..")
			continue;
		wire::Entry entry;
		entry.name = leaf;
		FillWireStat(ent->st, entry.stat);
		out.push_back(std::move(entry));
	}
	smb2_closedir(fSmb, dir);
	return BackendStatus::kOk;
}

BackendStatus SmbBackend::Open(const std::string& path, uint64_t& handle, uint64_t& size)
{
	if (!IsConnected())
		return BackendStatus::kTransportError;
	std::string sp = SharePath(path);
	struct smb2fh* fh = smb2_open(fSmb, sp.c_str(), O_RDONLY);
	if (fh == nullptr)
		return BackendStatus::kNotFound;

	size = 0;
	struct smb2_stat_64 st;
	std::memset(&st, 0, sizeof(st));
	if (smb2_stat(fSmb, sp.c_str(), &st) == 0)
		size = st.smb2_size;

	handle = fNextHandle++;
	fOpen[handle] = fh;
	return BackendStatus::kOk;
}

BackendStatus SmbBackend::Read(uint64_t handle, uint64_t offset, uint32_t length,
	std::vector<uint8_t>& out)
{
	auto it = fOpen.find(handle);
	if (it == fOpen.end())
		return BackendStatus::kBadHandle;
	struct smb2fh* fh = it->second;

	out.assign(length, 0);
	size_t got = 0;
	// Loop until the request is satisfied or EOF, since a read may come back short.
	while (got < length) {
		uint32_t want = static_cast<uint32_t>(length - got);
		int n = smb2_pread(fSmb, fh, out.data() + got, want, offset + got);
		// A real read is 0..want. With libsmb2's native-errno convention a failure returns -errno,
		// a large positive value greater than `want`, so treat anything out of range as an error.
		if (n < 0 || static_cast<uint32_t>(n) > want) {
			out.clear();
			return BackendStatus::kIoError;
		}
		if (n == 0)
			break; // EOF
		got += static_cast<size_t>(n);
	}
	out.resize(got);
	return BackendStatus::kOk;
}

BackendStatus SmbBackend::Close(uint64_t handle)
{
	auto it = fOpen.find(handle);
	if (it == fOpen.end())
		return BackendStatus::kBadHandle;
	smb2_close(fSmb, it->second);
	fOpen.erase(it);
	return BackendStatus::kOk;
}

namespace {
// Reject a request path that tries to escape the share via a ".." component (working agreement:
// validate paths on both ends, no path escape).
bool PathEscapes(const std::string& path)
{
	size_t i = 0;
	while (i < path.size()) {
		size_t slash = path.find('/', i);
		std::string comp = path.substr(i, slash == std::string::npos ? std::string::npos : slash - i);
		if (comp == "..")
			return true;
		if (slash == std::string::npos)
			break;
		i = slash + 1;
	}
	return false;
}
} // namespace

BackendStatus SmbBackend::OpenWrite(const std::string& path, uint64_t& handle)
{
	if (!IsConnected())
		return BackendStatus::kTransportError;
	if (PathEscapes(path))
		return BackendStatus::kInvalidRequest;
	std::string sp = SharePath(path);
	// O_CREAT (make it if new) but NOT O_TRUNC: opening an existing file for writing must not lose
	// its contents. Truncation, incl. the O_TRUNC-on-open case, comes through the truncate op.
	struct smb2fh* fh = smb2_open(fSmb, sp.c_str(), O_WRONLY | O_CREAT);
	if (fh == nullptr)
		return BackendStatus::kAccessDenied; // most often a read-only share or no write permission
	handle = fNextHandle++;
	fOpen[handle] = fh;
	return BackendStatus::kOk;
}

BackendStatus SmbBackend::Write(uint64_t handle, uint64_t offset, const std::vector<uint8_t>& data,
	uint64_t& written)
{
	written = 0;
	auto it = fOpen.find(handle);
	if (it == fOpen.end())
		return BackendStatus::kBadHandle;
	struct smb2fh* fh = it->second;
	size_t total = 0;
	while (total < data.size()) {
		uint32_t want = static_cast<uint32_t>(std::min<size_t>(data.size() - total, 1u << 20));
		int n = smb2_pwrite(fSmb, fh, data.data() + total, want, offset + total);
		// Native-errno convention: a failure returns -errno, a value greater than `want`.
		if (n < 0 || static_cast<uint32_t>(n) > want)
			return BackendStatus::kIoError;
		if (n == 0)
			break;
		total += static_cast<size_t>(n);
	}
	written = total;
	return BackendStatus::kOk;
}

BackendStatus SmbBackend::Mkdir(const std::string& path, uint32_t mode)
{
	(void)mode; // SMB creates the directory with server defaults
	if (!IsConnected())
		return BackendStatus::kTransportError;
	if (PathEscapes(path))
		return BackendStatus::kInvalidRequest;
	int rc = smb2_mkdir(fSmb, SharePath(path).c_str());
	return rc != 0 ? ErrnoToStatus(-rc) : BackendStatus::kOk;
}

BackendStatus SmbBackend::Unlink(const std::string& path)
{
	if (!IsConnected())
		return BackendStatus::kTransportError;
	if (PathEscapes(path))
		return BackendStatus::kInvalidRequest;
	std::string sp = SharePath(path);
	// A directory needs rmdir, a file needs unlink; pick by type.
	struct smb2_stat_64 st;
	std::memset(&st, 0, sizeof(st));
	bool isDir = smb2_stat(fSmb, sp.c_str(), &st) == 0 && st.smb2_type == SMB2_TYPE_DIRECTORY;
	int rc = isDir ? smb2_rmdir(fSmb, sp.c_str()) : smb2_unlink(fSmb, sp.c_str());
	return rc != 0 ? ErrnoToStatus(-rc) : BackendStatus::kOk;
}

BackendStatus SmbBackend::Rename(const std::string& from, const std::string& to)
{
	if (!IsConnected())
		return BackendStatus::kTransportError;
	if (PathEscapes(from) || PathEscapes(to))
		return BackendStatus::kInvalidRequest;
	int rc = smb2_rename(fSmb, SharePath(from).c_str(), SharePath(to).c_str());
	return rc != 0 ? ErrnoToStatus(-rc) : BackendStatus::kOk;
}

BackendStatus SmbBackend::Truncate(const std::string& path, uint64_t size)
{
	if (!IsConnected())
		return BackendStatus::kTransportError;
	if (PathEscapes(path))
		return BackendStatus::kInvalidRequest;
	int rc = smb2_truncate(fSmb, SharePath(path).c_str(), size);
	return rc != 0 ? ErrnoToStatus(-rc) : BackendStatus::kOk;
}

} // namespace fondamenta
} // namespace campiello
