// Query.h
//
// CNP distributed live query (M4): QUERY_OPEN, QUERY_RESULT, QUERY_UPDATE, QUERY_CLOSE. A client
// opens a query with a stable queryId and a predicate string; the responder runs a live query on
// its shared root, streams QUERY_RESULT batches (done=true on the last initial batch), then pushes
// QUERY_UPDATE frames as entries appear or disappear on the responder, until QUERY_CLOSE.
//
//   QUERY_OPEN   (0x40): request { queryId, query }        -> streamed QUERY_RESULT(s)
//   QUERY_RESULT (0x41): reply   { queryId, entries, done }
//   QUERY_UPDATE (0x42): async   { queryId, added, entry }
//   QUERY_CLOSE  (0x43): request { queryId }               -> reply Ok (empty map)
//
// The queryId (client-assigned) correlates the streamed results and updates to the open query,
// since they arrive across several frames and asynchronously (a single requestId cannot, because
// updates are unsolicited). Pure standard C++ (no Haiku dependency), built on Cbor.h / Frame.h,
// reusing wire::Entry (Listing.h).

#ifndef CAMPIELLO_TRAGHETTO_WIRE_QUERY_H
#define CAMPIELLO_TRAGHETTO_WIRE_QUERY_H

#include <cstdint>
#include <string>
#include <vector>

#include "Frame.h"
#include "Listing.h" // Entry, WriteEntry/ReadEntry, kMaxEntriesPerReply

namespace campiello {
namespace wire {

// A query predicate string is bounded like a path.
static const size_t kMaxQueryBytes = 4096;

// QUERY_OPEN request { queryId, query }.
std::vector<uint8_t> EncodeQueryOpenRequest(uint64_t queryId, const std::string& query);
bool DecodeQueryOpenRequest(const std::vector<uint8_t>& payload, uint64_t& queryId,
	std::string& query);

// QUERY_RESULT reply { queryId, entries, done }. `done` marks the last batch of the initial set.
std::vector<uint8_t> EncodeQueryResultReply(uint64_t queryId, const std::vector<Entry>& entries,
	bool done);
bool DecodeQueryResultReply(const std::vector<uint8_t>& payload, uint64_t& queryId,
	std::vector<Entry>& entries, bool& done);

// QUERY_UPDATE async { queryId, added, entry }. added=true: entry appeared; false: it went away.
std::vector<uint8_t> EncodeQueryUpdate(uint64_t queryId, bool added, const Entry& entry);
bool DecodeQueryUpdate(const std::vector<uint8_t>& payload, uint64_t& queryId, bool& added,
	Entry& entry);

// QUERY_CLOSE request { queryId }.
std::vector<uint8_t> EncodeQueryCloseRequest(uint64_t queryId);
bool DecodeQueryCloseRequest(const std::vector<uint8_t>& payload, uint64_t& queryId);

// Frame builders. Replies/updates carry the request's requestId (updates echo the open request's).
Frame MakeQueryOpenRequest(uint64_t queryId, const std::string& query, uint32_t requestId);
Frame MakeQueryResultReply(uint64_t queryId, const std::vector<Entry>& entries, bool done,
	uint32_t requestId);
Frame MakeQueryUpdate(uint64_t queryId, bool added, const Entry& entry, uint32_t requestId);
Frame MakeQueryCloseRequest(uint64_t queryId, uint32_t requestId);
Frame MakeQueryCloseReply(uint32_t requestId);

} // namespace wire
} // namespace campiello

#endif // CAMPIELLO_TRAGHETTO_WIRE_QUERY_H
