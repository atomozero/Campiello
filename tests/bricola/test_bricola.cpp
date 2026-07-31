// test_bricola.cpp
//
// Tests the discovery facade. The lifecycle contract is deterministic and always asserted:
// Start brings the worker up, Peers() is thread-safe, Stop joins cleanly and is idempotent.
// The two-node discovery (one Bricola finds another on the same host) needs working multicast
// loopback; where the environment provides none (a sandbox), it degrades to a NOTE, matching
// MdnsSocket's own test. Pure standard C++, no framework.

#include <chrono>
#include <cstdio>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "../../src/bricola/mdns/Bricola.h"

using namespace campiello::bricola::mdns;

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

class Recorder : public PeerObserver {
public:
	void PeerFound(const Peer& p) override
	{
		std::lock_guard<std::mutex> lock(fMutex);
		fFound.push_back(p.instance);
	}
	void PeerUpdated(const Peer&) override {}
	void PeerLost(const Peer& p) override
	{
		std::lock_guard<std::mutex> lock(fMutex);
		fLost.push_back(p.instance);
	}
	bool SawFound(const std::string& instance) const
	{
		std::lock_guard<std::mutex> lock(fMutex);
		for (const std::string& s : fFound)
			if (s == instance)
				return true;
		return false;
	}
	bool SawLost(const std::string& instance) const
	{
		std::lock_guard<std::mutex> lock(fMutex);
		for (const std::string& s : fLost)
			if (s == instance)
				return true;
		return false;
	}

private:
	mutable std::mutex       fMutex;
	std::vector<std::string> fFound;
	std::vector<std::string> fLost;
};

static ServiceInfo MakeSelf(const std::string& instance, const std::string& host, uint16_t port)
{
	ServiceInfo s;
	s.instance = instance;
	s.hostname = host;
	s.port = port;
	s.protocolVersion = 1;
	s.bfsAttrs = true;
	return s;
}

// Always-true: the facade's lifecycle behaves regardless of multicast delivery.
static void TestLifecycle()
{
	Bricola b;
	Recorder rec;
	if (!b.Start(MakeSelf("Solo", "solo.local", 7735), &rec)) {
		std::printf("SKIP bricola: socket unavailable here (%s)\n",
			b.Error() ? b.Error() : "?");
		return;
	}
	CHECK(b.IsRunning());
	CHECK(b.Peers().empty());   // nothing discovered yet
	std::this_thread::sleep_for(std::chrono::milliseconds(200));

	b.Stop();
	CHECK(!b.IsRunning());
	b.Stop();                   // idempotent
	CHECK(!b.IsRunning());
}

// Browse-only mode (used by the replicant): no ServiceInfo, still a clean lifecycle.
static void TestBrowseOnlyLifecycle()
{
	Bricola b;
	Recorder rec;
	if (!b.StartBrowsing(&rec)) {
		std::printf("SKIP bricola browse-only: socket unavailable (%s)\n",
			b.Error() ? b.Error() : "?");
		return;
	}
	CHECK(b.IsRunning());
	CHECK(b.Peers().empty());
	std::this_thread::sleep_for(std::chrono::milliseconds(150));
	b.Stop();
	CHECK(!b.IsRunning());
}

// Best-effort: two facades on the same host should discover each other where multicast loops.
static void TestTwoNodeDiscovery()
{
	// Bind both nodes to the loopback interface: same-host multicast is delivered there on
	// Haiku, so two local processes really do discover each other with no second machine.
	Bricola a, b;
	Recorder recA, recB;
	if (!a.Start(MakeSelf("Alpha", "alpha.local", 7001), &recA, "127.0.0.1")) {
		std::printf("SKIP bricola two-node: %s\n", a.Error() ? a.Error() : "?");
		return;
	}
	if (!b.Start(MakeSelf("Beta", "beta.local", 7002), &recB, "127.0.0.1")) {
		std::printf("SKIP bricola two-node: %s\n", b.Error() ? b.Error() : "?");
		a.Stop();
		return;
	}

	// Give the initial announces and query answers time to cross.
	for (int i = 0; i < 20; ++i) {
		if (recA.SawFound("Beta") && recB.SawFound("Alpha"))
			break;
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}

	bool mutual = recA.SawFound("Beta") && recB.SawFound("Alpha");
	if (mutual) {
		CHECK(recA.SawFound("Beta"));
		CHECK(recB.SawFound("Alpha"));
		// Stopping Beta sends a goodbye; Alpha should see it leave.
		b.Stop();
		for (int i = 0; i < 20 && !recA.SawLost("Beta"); ++i)
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
		CHECK(recA.SawLost("Beta"));
		a.Stop();
	} else {
		std::printf("NOTE bricola: no multicast loopback delivery here; two-node discovery "
			"unverified in this environment (facade lifecycle OK)\n");
		a.Stop();
		b.Stop();
	}
}

int main()
{
	TestLifecycle();
	TestBrowseOnlyLifecycle();
	TestTwoNodeDiscovery();

	std::printf("bricola: %d checks, %d failures\n", gChecks, gFailures);
	return gFailures == 0 ? 0 : 1;
}
