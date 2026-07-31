// Bricola.cpp
//
// See Bricola.h.

#include "Bricola.h"

#include <chrono>
#include <cstdlib>

namespace campiello {
namespace bricola {
namespace mdns {

namespace {

// How often to re-advertise (announce + browse query). Discovery on join is immediate (Start
// advertises at once); this refresh keeps peers alive well within the DNS-SD TTLs and catches
// any missed packets. Pragmatic M2 value; a full responder would ramp the interval.
const int64_t kAdvertiseIntervalMs = 30000;

// Receive timeout per loop, so Stop() is noticed promptly and the expiry tick runs regularly.
const int kPollMs = 500;

} // namespace

Bricola::~Bricola()
{
	Stop();
}

int64_t Bricola::NowMs()
{
	auto now = std::chrono::steady_clock::now().time_since_epoch();
	return std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
}

bool Bricola::Start(const ServiceInfo& self, PeerObserver* observer, const char* interfaceIpv4)
{
	return StartInternal(&self, observer, true, interfaceIpv4);
}

bool Bricola::StartBrowsing(PeerObserver* observer, const char* interfaceIpv4)
{
	return StartInternal(nullptr, observer, false, interfaceIpv4);
}

bool Bricola::StartInternal(const ServiceInfo* self, PeerObserver* observer, bool advertise,
	const char* interfaceIpv4)
{
	if (IsRunning()) {
		fError = "already running";
		return false;
	}

	// Resolve the multicast interface: explicit argument, else the CAMPIELLO_MDNS_IFACE env
	// override, else the primary non-loopback interface. Empty means INADDR_ANY.
	std::string iface = (interfaceIpv4 != nullptr) ? interfaceIpv4 : "";
	if (iface.empty()) {
		const char* env = std::getenv("CAMPIELLO_MDNS_IFACE");
		iface = (env != nullptr) ? env : PrimaryMulticastIPv4();
	}
	if (!fSocket.Open(iface.empty() ? nullptr : iface.c_str())) {
		fError = fSocket.Error();
		return false;
	}
	fUserObserver = observer;
	fAdvertise = advertise;
	fResponder = (advertise && self != nullptr) ? std::make_unique<Responder>(*self) : nullptr;
	// The upcast to PeerObserver* must happen here, inside Bricola, since the inheritance is
	// private and make_unique cannot see the base.
	fBrowser = std::make_unique<Browser>(static_cast<PeerObserver*>(this));
	{
		std::lock_guard<std::mutex> lock(fMutex);
		fSnapshot.clear();
	}
	fStop.store(false);
	fError = nullptr;
	fThread = std::thread(&Bricola::Run, this);
	return true;
}

void Bricola::Stop()
{
	if (!fThread.joinable())
		return;
	fStop.store(true);
	fThread.join();
	fSocket.Close();
	fResponder.reset();
	fBrowser.reset();
	fUserObserver = nullptr;
}

void Bricola::Advertise()
{
	if (fResponder) {
		std::string announce = fResponder->AnnouncePacket();
		fSocket.SendMulticast(announce.data(), announce.size());
	}
	std::string query = fBrowser->QueryPacket();
	fSocket.SendMulticast(query.data(), query.size());
}

void Bricola::Run()
{
	Advertise();
	int64_t lastAdvertise = NowMs();

	while (!fStop.load()) {
		uint8_t buf[2048];
		std::string srcIp;
		uint16_t srcPort = 0;
		int n = fSocket.Receive(buf, sizeof(buf), kPollMs, srcIp, srcPort);
		if (n > 0) {
			// Answer a browser asking for our service (only when advertising).
			if (fResponder) {
				std::string answer = fResponder->ResponseTo(buf, static_cast<size_t>(n));
				if (!answer.empty())
					fSocket.SendMulticast(answer.data(), answer.size());
			}
			// Fold whatever we heard (queries are ignored by the table) into the peer set.
			fBrowser->OnPacket(buf, static_cast<size_t>(n), srcIp, NowMs());
		}

		int64_t now = NowMs();
		fBrowser->Tick(now);
		if (now - lastAdvertise >= kAdvertiseIntervalMs) {
			Advertise();
			lastAdvertise = now;
		}
	}

	// Tell peers we are leaving so they drop us at once instead of waiting out the TTL. Only an
	// advertising node has an announcement to withdraw; a browse-only node has nothing to say.
	if (fResponder) {
		std::string bye = fResponder->GoodbyePacket();
		fSocket.SendMulticast(bye.data(), bye.size());
	}
}

void Bricola::PeerFound(const Peer& peer)
{
	{
		std::lock_guard<std::mutex> lock(fMutex);
		fSnapshot[peer.key] = peer;
	}
	if (fUserObserver != nullptr)
		fUserObserver->PeerFound(peer);
}

void Bricola::PeerUpdated(const Peer& peer)
{
	{
		std::lock_guard<std::mutex> lock(fMutex);
		fSnapshot[peer.key] = peer;
	}
	if (fUserObserver != nullptr)
		fUserObserver->PeerUpdated(peer);
}

void Bricola::PeerLost(const Peer& peer)
{
	{
		std::lock_guard<std::mutex> lock(fMutex);
		fSnapshot.erase(peer.key);
	}
	if (fUserObserver != nullptr)
		fUserObserver->PeerLost(peer);
}

std::vector<Peer> Bricola::Peers() const
{
	std::vector<Peer> out;
	std::lock_guard<std::mutex> lock(fMutex);
	out.reserve(fSnapshot.size());
	for (const auto& kv : fSnapshot)
		out.push_back(kv.second);
	return out;
}

} // namespace mdns
} // namespace bricola
} // namespace campiello
