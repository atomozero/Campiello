// LiveQueryClient.cpp - see LiveQueryClient.h.

#include "LiveQueryClient.h"

#include "../../traghetto/wire/Frame.h"
#include "../../traghetto/wire/Query.h"

namespace campiello {
namespace fondamenta {

LiveQueryClient::LiveQueryClient(net::FrameChannel& channel, uint64_t queryId)
	: fChannel(channel), fQueryId(queryId)
{
}

LiveQueryClient::~LiveQueryClient()
{
	Close();
}

bool LiveQueryClient::Open(const std::string& predicate, std::vector<wire::Entry>& initial,
	UpdateCallback cb)
{
	if (fOpen)
		return false;
	if (!fChannel.Send(wire::MakeQueryOpenRequest(fQueryId, predicate, 0)))
		return false;

	// Collect the initial result set (one or more QUERY_RESULT batches until done). On a dedicated
	// channel only this query's frames arrive, so no requestId/queryId demultiplexing is needed.
	initial.clear();
	for (;;) {
		wire::Frame frame;
		if (!fChannel.Receive(frame))
			return false;
		if (frame.type == wire::MessageType::kError)
			return false;
		if (frame.type != wire::MessageType::kQueryResult)
			continue; // ignore anything unexpected before the initial set completes
		uint64_t replyId = 0;
		bool done = false;
		std::vector<wire::Entry> batch;
		if (!wire::DecodeQueryResultReply(frame.payload, replyId, batch, done) || replyId != fQueryId)
			return false;
		for (wire::Entry& e : batch)
			initial.push_back(std::move(e));
		if (done)
			break;
	}

	fCallback = std::move(cb);
	fOpen = true;
	fStop.store(false);
	fThread = std::thread([this]() { ReceiveLoop(); });
	return true;
}

void LiveQueryClient::ReceiveLoop()
{
	while (!fStop.load()) {
		wire::Frame frame;
		if (!fChannel.Receive(frame))
			return; // channel closed (e.g. by Close() -> Shutdown) or error
		if (frame.type != wire::MessageType::kQueryUpdate)
			continue; // a late QUERY_RESULT batch or the QUERY_CLOSE ack: not an update
		Update update;
		uint64_t replyId = 0;
		if (!wire::DecodeQueryUpdate(frame.payload, replyId, update.added, update.entry))
			continue;
		if (replyId != fQueryId)
			continue;
		if (fCallback)
			fCallback(update);
	}
}

void LiveQueryClient::Close()
{
	if (!fOpen)
		return;
	fOpen = false;
	fStop.store(true);
	// Best-effort tell the peer to stop the query (a write, while the receive thread reads: safe on
	// a TLS 1.3 connection), then unblock the receive thread's Receive and join it.
	fChannel.Send(wire::MakeQueryCloseRequest(fQueryId, 0));
	fChannel.Shutdown();
	if (fThread.joinable())
		fThread.join();
}

} // namespace fondamenta
} // namespace campiello
