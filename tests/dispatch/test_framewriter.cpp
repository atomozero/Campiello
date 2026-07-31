// test_framewriter.cpp
//
// Tests for FrameWriter: it serialises all sends onto its own thread (so exactly one thread does
// SSL_write), preserves single-producer order, drains on Stop, and fails (for teardown) on a send
// error or a queue overflow. No Haiku dependency.

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <thread>
#include <vector>

#include "../../src/traghetto/dispatch/FrameWriter.h"
#include "../../src/traghetto/transport/FrameChannel.h"
#include "../../src/traghetto/wire/Frame.h"

using namespace campiello;
using campiello::net::FrameWriter;

static int gChecks = 0;
static int gFailures = 0;

#define CHECK(cond)                                                            \
	do {                                                                       \
		++gChecks;                                                             \
		if (!(cond)) {                                                         \
			++gFailures;                                                       \
			std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);        \
		}                                                                      \
	} while (0)

// A fake channel that records the request ids it is asked to send. It can be told to block each
// send (a stuck peer) or to fail sending (a broken connection).
class FakeChannel : public campiello::net::FrameChannel {
public:
	std::atomic<bool> block{false};
	std::atomic<bool> failSend{false};
	std::mutex mutex;
	std::vector<uint32_t> sent;

	bool Send(const wire::Frame& frame) override
	{
		while (block.load())
			std::this_thread::yield();
		if (failSend.load())
			return false;
		std::lock_guard<std::mutex> lock(mutex);
		sent.push_back(frame.requestId);
		return true;
	}
	bool Receive(wire::Frame&) override { return false; }
};

static wire::Frame FrameWithId(uint32_t id)
{
	wire::Frame f;
	f.type = wire::MessageType::kQueryUpdate;
	f.requestId = id;
	return f;
}

int main()
{
	// Basic: a single producer's frames are all sent, in order, and Stop drains the backlog.
	{
		FakeChannel ch;
		FrameWriter w(ch);
		w.Start();
		for (uint32_t i = 0; i < 100; ++i)
			w.Enqueue(FrameWithId(i));
		w.Stop();
		CHECK(ch.sent.size() == 100);
		bool ordered = true;
		for (uint32_t i = 0; i < ch.sent.size(); ++i)
			if (ch.sent[i] != i) ordered = false;
		CHECK(ordered);
		CHECK(!w.Failed());
	}

	// Many producers: every enqueued frame reaches the channel exactly once (count is exact; the
	// interleaving across threads is not constrained).
	{
		FakeChannel ch;
		FrameWriter w(ch);
		w.Start();
		std::vector<std::thread> producers;
		for (int t = 0; t < 4; ++t) {
			producers.emplace_back([&w, t]() {
				for (uint32_t i = 0; i < 50; ++i)
					w.Enqueue(FrameWithId(static_cast<uint32_t>(t) * 1000 + i));
			});
		}
		for (std::thread& p : producers)
			p.join();
		w.Stop();
		CHECK(ch.sent.size() == 200);
	}

	// A send failure marks the writer failed, so the reader loop can tear the connection down.
	{
		FakeChannel ch;
		ch.failSend.store(true);
		FrameWriter w(ch);
		w.Start();
		w.Enqueue(FrameWithId(1));
		// Give the writer a moment to attempt (and fail) the send.
		for (int i = 0; i < 100000 && !w.Failed(); ++i)
			std::this_thread::yield();
		CHECK(w.Failed());
		w.Stop();
	}

	// Overflow: a stuck peer (send blocks) plus a flood past the cap fails the writer rather than
	// buffering without bound.
	{
		FakeChannel ch;
		ch.block.store(true); // every send hangs
		FrameWriter w(ch, /*maxQueued=*/16);
		w.Start();
		for (uint32_t i = 0; i < 200; ++i)
			w.Enqueue(FrameWithId(i));
		CHECK(w.Failed());
		ch.block.store(false); // let the in-flight send return so the thread can exit
		w.Stop();
	}

	std::printf("%s: %d checks, %d failures\n",
		gFailures == 0 ? "PASS" : "FAIL", gChecks, gFailures);
	return gFailures == 0 ? 0 : 1;
}
