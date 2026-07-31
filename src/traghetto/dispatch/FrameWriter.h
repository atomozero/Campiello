// FrameWriter.h
//
// A single dedicated writer thread for one FrameChannel: every outbound frame (request replies and
// asynchronous server pushes like QUERY_UPDATE) is enqueued here, and exactly this one thread calls
// channel.Send(). Paired with a reader thread that only calls channel.Receive(), this gives the
// exact concurrency model a TLS 1.3 connection supports (one reader + one writer, no renegotiation),
// with no lock on the SSL object and never two concurrent SSL_write calls.
//
// Enqueue() is thread-safe and never blocks on the socket (so a slow client cannot stall the reader
// or a query looper); the queue is bounded, and overflowing it marks the writer failed so the
// connection is torn down rather than buffering without limit.

#ifndef CAMPIELLO_TRAGHETTO_DISPATCH_FRAMEWRITER_H
#define CAMPIELLO_TRAGHETTO_DISPATCH_FRAMEWRITER_H

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <thread>

#include "../transport/FrameChannel.h"
#include "../wire/Frame.h"

namespace campiello {
namespace net {

class FrameWriter {
public:
	// `maxQueued` bounds the backlog; enqueuing past it fails the writer (a stuck/slow peer).
	explicit FrameWriter(FrameChannel& channel, size_t maxQueued = 4096);
	~FrameWriter();

	FrameWriter(const FrameWriter&) = delete;
	FrameWriter& operator=(const FrameWriter&) = delete;

	// Spawn the writer thread. Call once.
	void Start();

	// Queue a frame to send. Thread-safe, non-blocking. A no-op once stopped or failed.
	void Enqueue(const wire::Frame& frame);

	// True if a Send failed or the queue overflowed: the connection should be torn down.
	bool Failed() const { return fFailed.load(); }

	// Stop the writer: send whatever is already queued (unless a send fails), then join the thread.
	// Idempotent.
	void Stop();

private:
	void Run();

	FrameChannel&           fChannel;
	const size_t            fMaxQueued;
	std::thread             fThread;
	std::mutex              fMutex;
	std::condition_variable fCv;
	std::deque<wire::Frame> fQueue;
	bool                    fStop = false;
	bool                    fStarted = false;
	std::atomic<bool>       fFailed{false};
};

} // namespace net
} // namespace campiello

#endif // CAMPIELLO_TRAGHETTO_DISPATCH_FRAMEWRITER_H
