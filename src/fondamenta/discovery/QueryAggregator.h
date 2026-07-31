// QueryAggregator.h
//
// M4 distributed query, client side: fan a single Haiku query predicate out to every reachable
// peer (via PeerBackend::Query), merge the matches into one set, dedup by (peer, path), and cap the
// total by a quota so a broad predicate cannot flood the client. This is the aggregation behind the
// virtual query folder the PathRouter exposes; it is pure standard C++ (no Haiku dependency), so the
// merge/dedup/quota logic is unit-tested off Haiku with fake peers.

#ifndef CAMPIELLO_FONDAMENTA_DISCOVERY_QUERYAGGREGATOR_H
#define CAMPIELLO_FONDAMENTA_DISCOVERY_QUERYAGGREGATOR_H

#include <cstddef>
#include <string>
#include <vector>

#include "../../traghetto/wire/Listing.h" // wire::Entry
#include "PathRouter.h"                    // PeerSource

namespace campiello {
namespace fondamenta {

// One query hit, tagged with the peer it came from. `entry.name` is the path relative to that
// peer's shared root (so results across subdirectories stay unique).
struct AggregatedEntry {
	std::string peer;
	wire::Entry entry;
};

// The default cap on total aggregated results.
static const size_t kDefaultQueryQuota = 4096;

// Run `predicate` against every peer in `source` and return the merged, deduped (by peer + path),
// quota-capped result set. Peers that are unreachable or do not support queries are skipped. The
// order is stable: peers in PeerNames() order, entries in each peer's reply order.
std::vector<AggregatedEntry> AggregateQuery(PeerSource& source, const std::string& predicate,
	size_t quota = kDefaultQueryQuota);

// A filesystem-safe, unique-per-call leaf name for a query hit shown in the flat virtual folder:
// "<peer> - <relpath with '/' folded to '-'>", disambiguated against `used` with a numeric suffix
// on collision. Inserts the chosen name into `used`.
std::string QueryLeafName(const std::string& peer, const std::string& relPath,
	std::vector<std::string>& used);

} // namespace fondamenta
} // namespace campiello

#endif // CAMPIELLO_FONDAMENTA_DISCOVERY_QUERYAGGREGATOR_H
