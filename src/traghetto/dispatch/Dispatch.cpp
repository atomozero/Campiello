// Dispatch.cpp
//
// Implementation of the request/response dispatch. See Dispatch.h.

#include "Dispatch.h"

#include <utility>

#include "FrameWriter.h"

namespace campiello {
namespace net {

void ServeConnection(FrameChannel& channel, RequestHandler& handler)
{
	// This thread is the sole reader (channel.Receive / SSL_read); a dedicated FrameWriter is the
	// sole writer (channel.Send / SSL_write). That split is exactly the concurrency a TLS 1.3
	// connection supports, and it lets the handler push asynchronous frames (live QUERY_UPDATE) onto
	// the same connection without ever racing the reply path.
	FrameWriter writer(channel);
	writer.Start();
	handler.SetFrameSink([&writer](const wire::Frame& frame) { writer.Enqueue(frame); });

	for (;;) {
		wire::Frame request;
		if (!channel.Receive(request))
			break; // peer closed or transport/protocol error

		wire::Frame reply = handler.Handle(request);
		// The dispatcher owns the correlation: a reply always echoes the request id.
		reply.requestId = request.requestId;
		writer.Enqueue(reply);
		if (writer.Failed())
			break;
	}

	// Stop async pushes before the writer goes away, then flush and join it.
	handler.SetFrameSink(nullptr);
	writer.Stop();
}

bool Client::Request(wire::Frame request, wire::Frame& reply)
{
	uint32_t id = fNextId++;
	fLastId = id;
	request.requestId = id;

	if (!fChannel.Send(request))
		return false;

	// A matching reply may already be stashed from a previous out-of-order receive.
	auto stashed = fPending.find(id);
	if (stashed != fPending.end()) {
		reply = std::move(stashed->second);
		fPending.erase(stashed);
		return true;
	}

	for (;;) {
		wire::Frame frame;
		if (!fChannel.Receive(frame))
			return false;
		if (frame.requestId == id) {
			reply = std::move(frame);
			return true;
		}
		// A reply for a different (pipelined) request: keep it for later.
		fPending[frame.requestId] = std::move(frame);
	}
}

} // namespace net
} // namespace campiello
