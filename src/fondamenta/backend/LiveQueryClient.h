// LiveQueryClient.h
//
// Client side of the M4 live distributed query. Unlike CnpBackend::Query (a one-shot fetch), this
// keeps a query open on a dedicated connection to one peer: it sends QUERY_OPEN, returns the initial
// QUERY_RESULT set, then runs a receive thread that delivers each live QUERY_UPDATE (a file appeared
// or disappeared on the peer) to a callback until Close(). A dedicated channel means the receive
// loop never competes with the peer's request/reply traffic, so no frame demultiplexing is needed.
//
// The callback runs on the receive thread; the caller is responsible for its own synchronisation.

#ifndef CAMPIELLO_FONDAMENTA_BACKEND_LIVEQUERYCLIENT_H
#define CAMPIELLO_FONDAMENTA_BACKEND_LIVEQUERYCLIENT_H

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <thread>
#include <vector>

#include "../../traghetto/transport/FrameChannel.h"
#include "../../traghetto/wire/Listing.h" // wire::Entry

namespace campiello {
namespace fondamenta {

class LiveQueryClient {
public:
	struct Update {
		bool        added;  // true: entry appeared (B_ENTRY_CREATED); false: it went away
		wire::Entry entry;  // entry.name is the path relative to the peer's shared root
	};
	using UpdateCallback = std::function<void(const Update&)>;

	// `channel` is used exclusively by this query for its lifetime. `queryId` correlates the peer's
	// results and updates to this query.
	LiveQueryClient(net::FrameChannel& channel, uint64_t queryId);
	~LiveQueryClient();

	LiveQueryClient(const LiveQueryClient&) = delete;
	LiveQueryClient& operator=(const LiveQueryClient&) = delete;

	// Open the query: send QUERY_OPEN, collect the initial result set into `initial`, then start the
	// receive thread that delivers live updates to `cb`. Returns false on transport/protocol error.
	bool Open(const std::string& predicate, std::vector<wire::Entry>& initial, UpdateCallback cb);

	// Send QUERY_CLOSE and stop the receive thread. Idempotent; also called by the destructor.
	void Close();

private:
	void ReceiveLoop();

	net::FrameChannel& fChannel;
	uint64_t           fQueryId;
	UpdateCallback     fCallback;
	std::thread        fThread;
	std::atomic<bool>  fStop{false};
	bool               fOpen = false;
};

} // namespace fondamenta
} // namespace campiello

#endif // CAMPIELLO_FONDAMENTA_BACKEND_LIVEQUERYCLIENT_H
