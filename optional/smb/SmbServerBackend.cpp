// SmbServerBackend.cpp
//
// See SmbServerBackend.h.

#include "SmbServerBackend.h"

#include <sys/stat.h>

namespace campiello {
namespace fondamenta {

void SplitServerPath(const std::string& path, std::string& share, std::string& rest)
{
	share.clear();
	rest.clear();
	size_t i = 0;
	while (i < path.size() && path[i] == '/')
		++i; // skip leading slashes
	if (i >= path.size())
		return; // root: share and rest stay empty
	size_t slash = path.find('/', i);
	if (slash == std::string::npos) {
		share = path.substr(i); // "/Public" -> share "Public", no remainder
		return;
	}
	share = path.substr(i, slash - i);
	rest = path.substr(slash); // keep the leading '/', e.g. "/a/b" (share root when empty)
}

SmbServerBackend::~SmbServerBackend()
{
	Disconnect();
}

BackendStatus SmbServerBackend::ConnectServer(const SmbConfig& config)
{
	fServer = config;
	fServer.share.clear();
	fServer.basePath.clear();

	SmbBackend enumerator;
	BackendStatus s = enumerator.EnumShares(fServer, fShares);
	if (s != BackendStatus::kOk) {
		fError = enumerator.Error();
		return s;
	}
	return BackendStatus::kOk;
}

void SmbServerBackend::Disconnect()
{
	fHandles.clear();
	for (auto& kv : fBackends) {
		if (kv.second)
			kv.second->Disconnect();
	}
	fBackends.clear();
	fShares.clear();
}

wire::Entry SmbServerBackend::DirEntry(const std::string& name)
{
	wire::Entry e;
	e.name = name;
	e.stat = wire::Stat{};
	e.stat.mode = S_IFDIR | 0555;
	return e;
}

SmbBackend* SmbServerBackend::ShareBackend(const std::string& share)
{
	auto it = fBackends.find(share);
	if (it != fBackends.end())
		return it->second.get();

	auto backend = std::unique_ptr<SmbBackend>(new SmbBackend());
	SmbConfig c = fServer;
	c.share = share;
	BackendStatus s = backend->Connect(c);
	if (s != BackendStatus::kOk) {
		fError = backend->Error();
		return nullptr;
	}
	SmbBackend* raw = backend.get();
	fBackends[share] = std::move(backend);
	return raw;
}

BackendStatus SmbServerBackend::Stat(const std::string& path, wire::Entry& out)
{
	std::string share, rest;
	SplitServerPath(path, share, rest);
	if (share.empty()) {
		out = DirEntry(""); // the server root
		return BackendStatus::kOk;
	}
	if (rest.empty()) {
		out = DirEntry(share); // a share: always a directory, no network stat
		return BackendStatus::kOk;
	}
	SmbBackend* backend = ShareBackend(share);
	if (backend == nullptr)
		return BackendStatus::kAccessDenied;
	return backend->Stat(rest, out);
}

BackendStatus SmbServerBackend::ReadDir(const std::string& path, std::vector<wire::Entry>& out)
{
	std::string share, rest;
	SplitServerPath(path, share, rest);
	if (share.empty()) {
		out.clear();
		for (const std::string& name : fShares)
			out.push_back(DirEntry(name)); // the shares, as directories
		return BackendStatus::kOk;
	}
	SmbBackend* backend = ShareBackend(share);
	if (backend == nullptr)
		return BackendStatus::kAccessDenied;
	// rest is "" for the share root (SmbBackend::ReadDir("") lists the share root) or "/sub".
	return backend->ReadDir(rest, out);
}

BackendStatus SmbServerBackend::Open(const std::string& path, uint64_t& handle, uint64_t& size)
{
	std::string share, rest;
	SplitServerPath(path, share, rest);
	if (share.empty() || rest.empty())
		return BackendStatus::kIsADirectory; // the root and a bare share are directories
	SmbBackend* backend = ShareBackend(share);
	if (backend == nullptr)
		return BackendStatus::kAccessDenied;
	uint64_t local = 0;
	BackendStatus s = backend->Open(rest, local, size);
	if (s != BackendStatus::kOk)
		return s;
	handle = fNextHandle++;
	fHandles[handle] = std::make_pair(backend, local);
	return BackendStatus::kOk;
}

BackendStatus SmbServerBackend::Read(uint64_t handle, uint64_t offset, uint32_t length,
	std::vector<uint8_t>& out)
{
	auto it = fHandles.find(handle);
	if (it == fHandles.end())
		return BackendStatus::kBadHandle;
	return it->second.first->Read(it->second.second, offset, length, out);
}

BackendStatus SmbServerBackend::Close(uint64_t handle)
{
	auto it = fHandles.find(handle);
	if (it == fHandles.end())
		return BackendStatus::kBadHandle;
	BackendStatus s = it->second.first->Close(it->second.second);
	fHandles.erase(it);
	return s;
}

BackendStatus SmbServerBackend::OpenWrite(const std::string& path, uint64_t& handle)
{
	std::string share, rest;
	SplitServerPath(path, share, rest);
	if (share.empty() || rest.empty())
		return BackendStatus::kIsADirectory; // cannot create a file at the root or as a share
	SmbBackend* backend = ShareBackend(share);
	if (backend == nullptr)
		return BackendStatus::kAccessDenied;
	uint64_t local = 0;
	BackendStatus s = backend->OpenWrite(rest, local);
	if (s != BackendStatus::kOk)
		return s;
	handle = fNextHandle++;
	fHandles[handle] = std::make_pair(backend, local);
	return BackendStatus::kOk;
}

BackendStatus SmbServerBackend::Write(uint64_t handle, uint64_t offset,
	const std::vector<uint8_t>& data, uint64_t& written)
{
	written = 0;
	auto it = fHandles.find(handle);
	if (it == fHandles.end())
		return BackendStatus::kBadHandle;
	return it->second.first->Write(it->second.second, offset, data, written);
}

BackendStatus SmbServerBackend::Mkdir(const std::string& path, uint32_t mode)
{
	std::string share, rest;
	SplitServerPath(path, share, rest);
	if (share.empty() || rest.empty())
		return BackendStatus::kAccessDenied; // shares are not created through the volume
	SmbBackend* backend = ShareBackend(share);
	if (backend == nullptr)
		return BackendStatus::kAccessDenied;
	return backend->Mkdir(rest, mode);
}

BackendStatus SmbServerBackend::Unlink(const std::string& path)
{
	std::string share, rest;
	SplitServerPath(path, share, rest);
	if (share.empty() || rest.empty())
		return BackendStatus::kAccessDenied; // cannot remove the root or a share
	SmbBackend* backend = ShareBackend(share);
	if (backend == nullptr)
		return BackendStatus::kAccessDenied;
	return backend->Unlink(rest);
}

BackendStatus SmbServerBackend::Rename(const std::string& from, const std::string& to)
{
	std::string fromShare, fromRest, toShare, toRest;
	SplitServerPath(from, fromShare, fromRest);
	SplitServerPath(to, toShare, toRest);
	if (fromShare.empty() || fromRest.empty() || toShare.empty() || toRest.empty())
		return BackendStatus::kAccessDenied;
	if (fromShare != toShare)
		return BackendStatus::kUnsupported; // SMB cannot rename across shares
	SmbBackend* backend = ShareBackend(fromShare);
	if (backend == nullptr)
		return BackendStatus::kAccessDenied;
	return backend->Rename(fromRest, toRest);
}

BackendStatus SmbServerBackend::Truncate(const std::string& path, uint64_t size)
{
	std::string share, rest;
	SplitServerPath(path, share, rest);
	if (share.empty() || rest.empty())
		return BackendStatus::kIsADirectory;
	SmbBackend* backend = ShareBackend(share);
	if (backend == nullptr)
		return BackendStatus::kAccessDenied;
	return backend->Truncate(rest, size);
}

} // namespace fondamenta
} // namespace campiello
