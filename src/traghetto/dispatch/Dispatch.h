// Dispatch.h
//
// Request/response dispatch over a FrameChannel: the server serve-loop and the client
// correlator. This turns the transport into a real CNP client/server. Transport-agnostic
// (works over the plain Connection or the TLS TlsConnection); the daemon runs
// ServeConnection on a thread per peer (docs/REUSE.md), and Fondamenta's CnpBackend uses a
// Client per peer.

#ifndef CAMPIELLO_TRAGHETTO_DISPATCH_DISPATCH_H
#define CAMPIELLO_TRAGHETTO_DISPATCH_DISPATCH_H

#include <cstdint>
#include <functional>
#include <map>
#include <memory>

#include "../tls/Fingerprint.h"
#include "../transport/FrameChannel.h"
#include "../wire/Frame.h"

namespace campiello {
namespace net {

// Turns a request frame into a reply frame. Implemented by the daemon/backend. The reply's
// request_id is set by the dispatcher to match the request, so handlers need not set it.
// A sink the dispatcher gives a handler so it can push unsolicited frames (e.g. live QUERY_UPDATE)
// onto the connection's writer thread. Thread-safe to call from any thread the handler owns.
using FrameSink = std::function<void(const wire::Frame&)>;

class RequestHandler {
public:
	virtual ~RequestHandler() = default;
	virtual wire::Frame Handle(const wire::Frame& request) = 0;

	// Install (or clear, with a null sink) the async push channel for this connection. The
	// dispatcher calls this once with a live sink before the serve loop and once with null before
	// the writer stops. Handlers that never push asynchronously ignore it (the default).
	virtual void SetFrameSink(FrameSink /*sink*/) {}
};

// Creates a fresh RequestHandler per connection, so each peer gets its own state (e.g. an
// open-file table) that is cleaned up when the connection ends. The daemon calls Create()
// once per accepted peer, passing the peer's authenticated SPKI fingerprint (all-zero over
// the plain transport, which carries no identity). Handlers that do not gate on identity
// simply ignore it.
class HandlerFactory {
public:
	virtual ~HandlerFactory() = default;
	virtual std::unique_ptr<RequestHandler> Create(const Fingerprint& peer) = 0;
};

// Serve requests on one channel until it closes or errors. Blocking; the daemon calls this
// on a dedicated thread per connected peer.
void ServeConnection(FrameChannel& channel, RequestHandler& handler);

// Sends requests and matches replies by request_id over one channel. Not thread-safe; use
// one Client per channel/thread. Handles out-of-order replies (stashes ones that arrive
// before their matching request is awaited), so a caller may pipeline.
class Client {
public:
	explicit Client(FrameChannel& channel) : fChannel(channel) {}

	// Assign a fresh request_id to `request`, send it, and block for its reply. False on
	// transport failure. On success `reply` holds the matching frame (which may be an
	// ERROR frame the caller inspects).
	bool Request(wire::Frame request, wire::Frame& reply);

	// The request_id assigned to the most recent Request() call.
	uint32_t LastRequestId() const { return fLastId; }

private:
	FrameChannel& fChannel;
	uint32_t fNextId = 1;
	uint32_t fLastId = 0;
	std::map<uint32_t, wire::Frame> fPending;
};

} // namespace net
} // namespace campiello

#endif // CAMPIELLO_TRAGHETTO_DISPATCH_DISPATCH_H
