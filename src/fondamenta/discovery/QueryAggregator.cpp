// QueryAggregator.cpp - see QueryAggregator.h.

#include "QueryAggregator.h"

#include <set>

namespace campiello {
namespace fondamenta {

std::vector<AggregatedEntry> AggregateQuery(PeerSource& source, const std::string& predicate,
	size_t quota)
{
	std::vector<AggregatedEntry> out;
	std::set<std::string> seen; // "peer\0relpath" dedup key
	for (const std::string& peer : source.PeerNames()) {
		if (out.size() >= quota)
			break;
		PeerBackend* backend = source.BackendFor(peer);
		if (backend == nullptr)
			continue; // known peer, not reachable right now
		std::vector<wire::Entry> hits;
		if (backend->Query(predicate, hits) != BackendStatus::kOk)
			continue; // peer does not support queries, or the query failed
		for (wire::Entry& e : hits) {
			if (out.size() >= quota)
				break;
			std::string key = peer;
			key.push_back('\0');
			key += e.name;
			if (!seen.insert(key).second)
				continue; // same (peer, path) already seen
			AggregatedEntry ae;
			ae.peer = peer;
			ae.entry = std::move(e);
			out.push_back(std::move(ae));
		}
	}
	return out;
}

std::string QueryLeafName(const std::string& peer, const std::string& relPath,
	std::vector<std::string>& used)
{
	std::string flat = relPath;
	for (char& c : flat)
		if (c == '/')
			c = '-';
	std::string base = peer + " - " + flat;
	std::string name = base;
	int suffix = 2;
	while (true) {
		bool clash = false;
		for (const std::string& u : used)
			if (u == name) { clash = true; break; }
		if (!clash)
			break;
		name = base + " (" + std::to_string(suffix++) + ")";
	}
	used.push_back(name);
	return name;
}

} // namespace fondamenta
} // namespace campiello
