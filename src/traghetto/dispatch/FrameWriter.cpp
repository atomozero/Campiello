// FrameWriter.cpp - see FrameWriter.h.

#include "FrameWriter.h"

#include <utility>

namespace campiello {
namespace net {

FrameWriter::FrameWriter(FrameChannel& channel, size_t maxQueued)
	: fChannel(channel), fMaxQueued(maxQueued)
{
}

FrameWriter::~FrameWriter()
{
	Stop();
}

void FrameWriter::Start()
{
	std::lock_guard<std::mutex> lock(fMutex);
	if (fStarted)
		return;
	fStarted = true;
	fThread = std::thread([this]() { Run(); });
}

void FrameWriter::Enqueue(const wire::Frame& frame)
{
	std::lock_guard<std::mutex> lock(fMutex);
	if (fStop || fFailed.load())
		return;
	if (fQueue.size() >= fMaxQueued) {
		// A peer that will not drain its socket: fail rather than buffer without bound.
		fFailed.store(true);
		fCv.notify_all();
		return;
	}
	fQueue.push_back(frame);
	fCv.notify_one();
}

void FrameWriter::Stop()
{
	{
		std::lock_guard<std::mutex> lock(fMutex);
		if (!fStarted) {
			fStop = true;
			return;
		}
		fStop = true;
		fCv.notify_all();
	}
	if (fThread.joinable())
		fThread.join();
}

void FrameWriter::Run()
{
	for (;;) {
		wire::Frame frame;
		{
			std::unique_lock<std::mutex> lock(fMutex);
			fCv.wait(lock, [this]() { return !fQueue.empty() || fStop || fFailed.load(); });
			if (fFailed.load())
				return;
			if (fQueue.empty()) {
				// Woken to stop with nothing left to send: done. (On stop we drain first, so an
				// empty queue here means everything queued has been sent.)
				if (fStop)
					return;
				continue;
			}
			frame = std::move(fQueue.front());
			fQueue.pop_front();
		}
		// Send outside the lock so Enqueue never blocks on the socket.
		if (!fChannel.Send(frame)) {
			fFailed.store(true);
			return;
		}
	}
}

} // namespace net
} // namespace campiello
