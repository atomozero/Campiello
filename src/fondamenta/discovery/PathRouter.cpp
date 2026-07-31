// PathRouter.cpp
//
// See PathRouter.h.

#include "PathRouter.h"

#include <sys/stat.h>

#include "QueryAggregator.h"

namespace campiello {
namespace fondamenta {

namespace {

// A synthetic read-only directory stat for the root and the per-peer folders.
void FillDirStat(wire::Stat& s)
{
	s = wire::Stat{};
	s.mode = S_IFDIR | 0555; // read + traverse, no write
	s.size = 0;
	s.inode = 0;
}

// A synthetic read-only file stat for an aggregated query-result leaf (a listing entry).
void FillFileStat(wire::Stat& s)
{
	s = wire::Stat{};
	s.mode = S_IFREG | 0444;
	s.size = 0;
	s.inode = 0;
}

// The reserved top-level folder that turns a predicate subfolder into a live distributed query:
// /.query/<predicate> lists the aggregated matches from every peer (like a BeOS query folder).
const char* const kQueryDir = ".query";

enum class QueryKind { None, Root, Predicate, Result };

// Classify a path against the query folder. For Predicate, `predicate` is the folder name; for
// Result, `leaf` is the result entry name below it.
QueryKind ClassifyQuery(const std::string& path, std::string& predicate, std::string& leaf)
{
	const std::string base = std::string("/") + kQueryDir;
	if (path == base)
		return QueryKind::Root;
	const std::string prefix = base + "/";
	if (path.size() <= prefix.size() || path.compare(0, prefix.size(), prefix) != 0)
		return QueryKind::None;
	std::string rest = path.substr(prefix.size()); // after "/.query/"
	size_t slash = rest.find('/');
	if (slash == std::string::npos) {
		predicate = rest;
		return predicate.empty() ? QueryKind::None : QueryKind::Predicate;
	}
	predicate = rest.substr(0, slash);
	leaf = rest.substr(slash + 1);
	if (predicate.empty() || leaf.empty() || leaf.find('/') != std::string::npos)
		return QueryKind::None;
	return QueryKind::Result;
}

} // namespace

bool PathRouter::SplitPeer(const std::string& path, std::string& peer, std::string& rest) const
{
	if (path.empty() || path == "/")
		return false;
	size_t start = (path[0] == '/') ? 1 : 0;
	size_t slash = path.find('/', start);
	if (slash == std::string::npos) {
		peer = path.substr(start);
		rest = "/";
	} else {
		peer = path.substr(start, slash - start);
		rest = path.substr(slash); // includes the leading '/'
		if (rest.empty())
			rest = "/";
	}
	return !peer.empty();
}

bool PathRouter::PeerExists(const std::string& peer)
{
	for (const std::string& name : fSource.PeerNames()) {
		if (name == peer)
			return true;
	}
	return false;
}

BackendStatus PathRouter::Stat(const std::string& path, wire::Entry& out)
{
	if (path.empty() || path == "/") {
		out.name = "/";
		out.attrs.clear();
		FillDirStat(out.stat);
		return BackendStatus::kOk;
	}

	// The virtual query folder and its predicate subfolders are synthetic directories; a result
	// below a predicate is a synthetic file (a listing entry).
	{
		std::string predicate, leaf;
		QueryKind qk = ClassifyQuery(path, predicate, leaf);
		if (qk == QueryKind::Root || qk == QueryKind::Predicate) {
			out.name = (qk == QueryKind::Root) ? kQueryDir : predicate;
			out.attrs.clear();
			FillDirStat(out.stat);
			return BackendStatus::kOk;
		}
		if (qk == QueryKind::Result) {
			out.name = leaf;
			out.attrs.clear();
			FillFileStat(out.stat);
			return BackendStatus::kOk;
		}
	}

	std::string peer, rest;
	if (!SplitPeer(path, peer, rest))
		return BackendStatus::kNotFound;
	if (!PeerExists(peer))
		return BackendStatus::kNotFound;

	if (rest == "/") {
		// The peer folder itself: a synthetic directory (no connection needed).
		out.name = peer;
		out.attrs.clear();
		FillDirStat(out.stat);
		return BackendStatus::kOk;
	}

	PeerBackend* backend = fSource.BackendFor(peer);
	if (backend == nullptr)
		return BackendStatus::kTransportError; // known peer, not reachable right now
	return backend->Stat(rest, out);
}

BackendStatus PathRouter::ReadDir(const std::string& path, std::vector<wire::Entry>& out)
{
	if (path.empty() || path == "/") {
		out.clear();
		for (const std::string& name : fSource.PeerNames()) {
			wire::Entry entry;
			entry.name = name;
			entry.attrs.clear();
			FillDirStat(entry.stat);
			out.push_back(std::move(entry));
		}
		// The reserved query folder sits alongside the peers.
		wire::Entry q;
		q.name = kQueryDir;
		q.attrs.clear();
		FillDirStat(q.stat);
		out.push_back(std::move(q));
		return BackendStatus::kOk;
	}

	// Query folder: an empty listing at the root (you create a predicate subfolder), and the live
	// aggregated matches under a predicate.
	{
		std::string predicate, leaf;
		QueryKind qk = ClassifyQuery(path, predicate, leaf);
		if (qk == QueryKind::Root) {
			out.clear();
			return BackendStatus::kOk;
		}
		if (qk == QueryKind::Predicate) {
			out.clear();
			std::vector<AggregatedEntry> hits = AggregateQuery(fSource, predicate);
			std::vector<std::string> used;
			for (AggregatedEntry& a : hits) {
				wire::Entry e = std::move(a.entry);
				e.name = QueryLeafName(a.peer, e.name, used);
				out.push_back(std::move(e));
			}
			return BackendStatus::kOk;
		}
		if (qk == QueryKind::Result)
			return BackendStatus::kNotADirectory; // a result leaf is a file, not a directory
	}

	std::string peer, rest;
	if (!SplitPeer(path, peer, rest))
		return BackendStatus::kNotFound;
	if (!PeerExists(peer))
		return BackendStatus::kNotFound;

	PeerBackend* backend = fSource.BackendFor(peer);
	if (backend == nullptr)
		return BackendStatus::kTransportError;
	return backend->ReadDir(rest, out);
}

BackendStatus PathRouter::Open(const std::string& path, uint64_t& handle, uint64_t& size)
{
	{
		std::string predicate, leaf;
		QueryKind qk = ClassifyQuery(path, predicate, leaf);
		if (qk == QueryKind::Root || qk == QueryKind::Predicate)
			return BackendStatus::kIsADirectory;
		if (qk == QueryKind::Result)
			// Reading a result in place is a follow-up: open the file under its peer folder
			// (/<peer>/<path>) instead. The aggregated folder is a listing for now.
			return BackendStatus::kUnsupported;
	}

	std::string peer, rest;
	if (!SplitPeer(path, peer, rest))
		return BackendStatus::kIsADirectory; // "/" is a directory
	if (rest == "/")
		return BackendStatus::kIsADirectory; // "/<peer>" is a directory
	if (!PeerExists(peer))
		return BackendStatus::kNotFound;

	PeerBackend* backend = fSource.BackendFor(peer);
	if (backend == nullptr)
		return BackendStatus::kTransportError;

	uint64_t backendHandle = 0;
	BackendStatus status = backend->Open(rest, backendHandle, size);
	if (status != BackendStatus::kOk)
		return status;

	handle = fNextHandle++;
	fOpen[handle] = OpenHandle{backend, backendHandle};
	return BackendStatus::kOk;
}

BackendStatus PathRouter::Read(uint64_t handle, uint64_t offset, uint32_t length,
	std::vector<uint8_t>& out)
{
	auto it = fOpen.find(handle);
	if (it == fOpen.end())
		return BackendStatus::kBadHandle;
	return it->second.backend->Read(it->second.backendHandle, offset, length, out);
}

BackendStatus PathRouter::Close(uint64_t handle)
{
	auto it = fOpen.find(handle);
	if (it == fOpen.end())
		return BackendStatus::kBadHandle;
	BackendStatus status = it->second.backend->Close(it->second.backendHandle);
	fOpen.erase(it);
	return status;
}

} // namespace fondamenta
} // namespace campiello
