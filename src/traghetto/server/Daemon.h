// Daemon.h
//
// The multi-peer CNP daemon: accept connections, wrap each in a channel (TLS handshake in
// native mode), and serve it on a dedicated thread with a RequestHandler. Ties together the
// Listener, a ChannelFactory, and a handler; Stop() shuts everything down cleanly.
//
// A ChannelFactory abstracts how an accepted socket becomes a FrameChannel, so the daemon
// is transport-agnostic: PlainChannelFactory for tests, TlsChannelFactory for native mode.

#ifndef CAMPIELLO_TRAGHETTO_SERVER_DAEMON_H
#define CAMPIELLO_TRAGHETTO_SERVER_DAEMON_H

#include <atomic>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "../dispatch/Dispatch.h"
#include "../tls/TlsConnection.h"
#include "../transport/Connection.h"
#include "../transport/FrameChannel.h"

namespace campiello {
namespace net {

// Wraps an accepted socket fd into a FrameChannel. Takes ownership of the fd. Returns null
// if the connection is rejected (e.g. TLS handshake or pin failure).
class ChannelFactory {
public:
	virtual ~ChannelFactory() = default;
	virtual std::unique_ptr<FrameChannel> Wrap(int fd) = 0;
};

// Wraps the fd in a plain (unencrypted) Connection. For tests and interop scaffolding.
class PlainChannelFactory : public ChannelFactory {
public:
	std::unique_ptr<FrameChannel> Wrap(int fd) override;
};

// Runs the TLS handshake over the fd and, on success, returns a TlsConnection. The peer's
// fingerprint is captured (trust-on-first-use); a real deployment would pass a pinning
// policy here.
class TlsChannelFactory : public ChannelFactory {
public:
	explicit TlsChannelFactory(TlsContext& context) : fContext(context) {}
	std::unique_ptr<FrameChannel> Wrap(int fd) override;

private:
	TlsContext& fContext;
};

class Daemon {
public:
	// The listener must already be Listen()ing. References must outlive the daemon. The
	// handler factory produces one handler per connected peer.
	Daemon(Listener& listener, ChannelFactory& factory, HandlerFactory& handlerFactory)
		: fListener(listener), fFactory(factory), fHandlerFactory(handlerFactory) {}
	~Daemon();

	Daemon(const Daemon&) = delete;
	Daemon& operator=(const Daemon&) = delete;

	// Start the accept loop on a background thread. False if the listener is not open.
	bool Start();

	// Stop accepting, unblock and join all peer threads. Idempotent; also called by the
	// destructor.
	void Stop();

private:
	void AcceptLoop();

	Listener& fListener;
	ChannelFactory& fFactory;
	HandlerFactory& fHandlerFactory;

	std::thread fAcceptThread;
	std::mutex fMutex;
	std::vector<std::thread> fPeerThreads;      // guarded by fMutex during accept
	std::vector<FrameChannel*> fActiveChannels; // guarded by fMutex
	std::atomic<bool> fStopping{false};
};

} // namespace net
} // namespace campiello

#endif // CAMPIELLO_TRAGHETTO_SERVER_DAEMON_H
