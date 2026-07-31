// Daemon.cpp
//
// Implementation of the multi-peer daemon. See Daemon.h.

#include "Daemon.h"

#include <algorithm>
#include <unistd.h>

namespace campiello {
namespace net {

std::unique_ptr<FrameChannel> PlainChannelFactory::Wrap(int fd)
{
	return std::unique_ptr<FrameChannel>(new Connection(fd));
}

std::unique_ptr<FrameChannel> TlsChannelFactory::Wrap(int fd)
{
	auto conn = std::unique_ptr<TlsConnection>(new TlsConnection());
	if (!TlsConnection::Accept(fContext, fd, /*expectedPeer=*/nullptr, *conn))
		return nullptr;
	return conn;
}

Daemon::~Daemon()
{
	Stop();
}

bool Daemon::Start()
{
	if (!fListener.IsOpen())
		return false;
	fAcceptThread = std::thread([this]() { AcceptLoop(); });
	return true;
}

void Daemon::AcceptLoop()
{
	for (;;) {
		int fd = fListener.AcceptRawFd();
		if (fd < 0)
			return; // listener closed by Stop(), or an accept error
		if (fStopping.load()) {
			::close(fd);
			return;
		}

		std::unique_ptr<FrameChannel> channel = fFactory.Wrap(fd);
		if (!channel)
			continue; // rejected (e.g. TLS handshake / pin failure)

		std::unique_ptr<RequestHandler> handler
			= fHandlerFactory.Create(channel->PeerFingerprint());
		if (!handler)
			continue;

		FrameChannel* raw = channel.get();
		std::lock_guard<std::mutex> lock(fMutex);
		fActiveChannels.push_back(raw);
		fPeerThreads.emplace_back(
			[this, channel = std::move(channel), handler = std::move(handler), raw]() mutable {
				ServeConnection(*channel, *handler);
				// Unregister on the way out; `channel` and `handler` are destroyed here,
				// closing the peer connection and its open files.
				std::lock_guard<std::mutex> lock(fMutex);
				fActiveChannels.erase(
					std::remove(fActiveChannels.begin(), fActiveChannels.end(), raw),
					fActiveChannels.end());
			});
	}
}

void Daemon::Stop()
{
	bool expected = false;
	if (!fStopping.compare_exchange_strong(expected, true))
		return; // already stopped/stopping

	// Break the accept loop.
	fListener.Close();
	if (fAcceptThread.joinable())
		fAcceptThread.join();

	// Unblock every peer thread's Receive by shutting down its socket.
	{
		std::lock_guard<std::mutex> lock(fMutex);
		for (FrameChannel* channel : fActiveChannels)
			channel->Shutdown();
	}

	// The accept loop has stopped, so fPeerThreads is no longer being appended to.
	for (std::thread& thread : fPeerThreads) {
		if (thread.joinable())
			thread.join();
	}
	fPeerThreads.clear();
	fActiveChannels.clear();
}

} // namespace net
} // namespace campiello
