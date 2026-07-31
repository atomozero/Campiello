// PeerTable.cpp
//
// See PeerTable.h.

#include "PeerTable.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <utility>

namespace campiello {
namespace bricola {
namespace mdns {

namespace {

std::string Lower(const std::string& s)
{
	std::string out = s;
	std::transform(out.begin(), out.end(), out.begin(),
		[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return out;
}

bool EndsWith(const std::string& s, const std::string& suffix)
{
	return s.size() >= suffix.size()
		&& s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

// Parse a decimal string into a bounded unsigned; returns false on empty/non-numeric input.
bool ParseUint(const std::string& s, unsigned long& out)
{
	if (s.empty())
		return false;
	char* end = nullptr;
	unsigned long v = std::strtoul(s.c_str(), &end, 10);
	if (end == s.c_str() || *end != '\0')
		return false;
	out = v;
	return true;
}

// The peer fields that matter to an observer; expiry and internal flags are excluded so a bare
// TTL refresh does not look like a change.
bool ContentEqual(const Peer& a, const Peer& b)
{
	return a.instance == b.instance && a.hostname == b.hostname
		&& a.addresses == b.addresses && a.port == b.port
		&& a.protocolVersion == b.protocolVersion && a.bfsAttrs == b.bfsAttrs
		&& a.caps == b.caps && a.fingerprintHex == b.fingerprintHex;
}

int64_t ExpiryFrom(int64_t nowMs, uint32_t ttlSeconds)
{
	return nowMs + static_cast<int64_t>(ttlSeconds) * 1000;
}

} // namespace

PeerTable::PeerTable(std::string serviceName, PeerObserver* observer)
	:
	fService(std::move(serviceName)),
	fObserver(observer)
{
}

std::string PeerTable::FriendlyLabel(const std::string& instanceFqdn) const
{
	std::string suffix = "." + fService;
	if (EndsWith(instanceFqdn, suffix))
		return instanceFqdn.substr(0, instanceFqdn.size() - suffix.size());
	return instanceFqdn;
}

bool PeerTable::AttachAddresses(Peer& peer)
{
	if (peer.hostname.empty())
		return false;
	auto it = fHostAddrs.find(Lower(peer.hostname));
	if (it == fHostAddrs.end())
		return false;
	bool changed = false;
	for (const std::string& ip : it->second) {
		if (std::find(peer.addresses.begin(), peer.addresses.end(), ip)
			== peer.addresses.end()) {
			peer.addresses.push_back(ip);
			changed = true;
		}
	}
	return changed;
}

void PeerTable::Publish(Entry& entry)
{
	bool connectable = entry.peer.port != 0 && !entry.peer.addresses.empty();
	if (!entry.announced) {
		if (connectable) {
			entry.announced = true;
			if (fObserver != nullptr)
				fObserver->PeerFound(entry.peer);
		}
		return;   // still pending: stay silent until connectable
	}
	if (fObserver != nullptr)
		fObserver->PeerUpdated(entry.peer);
}

void PeerTable::Ingest(const uint8_t* buf, size_t len, const std::string& srcIp, int64_t nowMs)
{
	Message msg;
	if (!Parse(buf, len, msg))
		return;

	std::vector<const Record*> records;
	for (const Record& r : msg.answers)
		records.push_back(&r);
	for (const Record& r : msg.additionals)
		records.push_back(&r);

	const std::string instanceSuffix = "." + fService;

	// Pass 1: harvest A records into the host -> address map (order-independent resolution).
	for (const Record* r : records) {
		if (r->type != kTypeA)
			continue;
		std::string ip;
		if (DecodeA(*r, ip))
			fHostAddrs[Lower(r->name)].insert(ip);
	}

	// Pass 2: PTR / SRV / TXT touch or remove peer entries. `before` snapshots each touched
	// entry's pre-state so we can tell a real change from a bare refresh. PTR/SRV targets are
	// decoded against the whole buffer so DNS compression pointers resolve.
	std::set<std::string>        touched;
	std::set<std::string>        removed;
	std::map<std::string, Peer>  before;

	auto touch = [&](const std::string& key) -> Entry& {
		auto it = fPeers.find(key);
		if (it == fPeers.end()) {
			Entry e;
			e.peer.key = key;
			e.peer.instance = FriendlyLabel(key);
			it = fPeers.emplace(key, std::move(e)).first;
		}
		if (touched.insert(key).second)
			before[key] = it->second.peer;
		return it->second;
	};

	for (const Record* r : records) {
		if (r->type == kTypePTR) {
			if (r->name != fService)
				continue;   // a PTR for some other service type
			std::string target;
			if (!DecodePtr(buf, len, *r, target))
				continue;
			if (r->ttl == 0) { removed.insert(target); continue; }
			Entry& e = touch(target);
			e.peer.expiresAtMs = std::max(e.peer.expiresAtMs, ExpiryFrom(nowMs, r->ttl));
		} else if (r->type == kTypeSRV) {
			if (!EndsWith(r->name, instanceSuffix))
				continue;
			if (r->ttl == 0) { removed.insert(r->name); continue; }
			uint16_t prio = 0, weight = 0, port = 0;
			std::string host;
			if (!DecodeSrv(buf, len, *r, prio, weight, port, host))
				continue;
			Entry& e = touch(r->name);
			e.peer.port = port;
			e.peer.hostname = host;
			e.peer.expiresAtMs = std::max(e.peer.expiresAtMs, ExpiryFrom(nowMs, r->ttl));
		} else if (r->type == kTypeTXT) {
			if (!EndsWith(r->name, instanceSuffix))
				continue;
			if (r->ttl == 0) { removed.insert(r->name); continue; }
			std::vector<std::pair<std::string, std::string>> kv;
			if (!DecodeTxt(*r, kv))
				continue;
			Entry& e = touch(r->name);
			for (const auto& pair : kv) {
				const std::string& k = pair.first;
				const std::string& v = pair.second;
				if (k == "v") {
					unsigned long n;
					if (ParseUint(v, n) && n <= 255)
						e.peer.protocolVersion = static_cast<uint8_t>(n);
				} else if (k == "node") {
					if (!v.empty())
						e.peer.instance = v;
				} else if (k == "bfs") {
					e.peer.bfsAttrs = (v == "1");
				} else if (k == "caps") {
					e.peer.caps = v;
				} else if (k == "fp") {
					e.peer.fingerprintHex = v;
				} else if (k == "port") {
					unsigned long n;
					if (e.peer.port == 0 && ParseUint(v, n) && n <= 0xFFFF)
						e.peer.port = static_cast<uint16_t>(n);
				}
			}
			e.peer.expiresAtMs = std::max(e.peer.expiresAtMs, ExpiryFrom(nowMs, r->ttl));
		}
	}

	// Removals (goodbyes) win over touches in the same packet.
	for (const std::string& key : removed) {
		auto it = fPeers.find(key);
		if (it == fPeers.end())
			continue;
		if (it->second.announced && fObserver != nullptr)
			fObserver->PeerLost(it->second.peer);
		fPeers.erase(it);
		touched.erase(key);
	}

	// Resolve addresses and publish each touched peer.
	for (const std::string& key : touched) {
		auto it = fPeers.find(key);
		if (it == fPeers.end())
			continue;
		Entry& e = it->second;
		AttachAddresses(e.peer);
		// Fallback: an unresolved but connectable peer is reachable at the packet's sender.
		if (e.peer.addresses.empty() && e.peer.port != 0 && !srcIp.empty())
			e.peer.addresses.push_back(srcIp);
		auto b = before.find(key);
		bool changed = b == before.end() || !ContentEqual(b->second, e.peer);
		if (changed || !e.announced)
			Publish(e);
	}
}

void PeerTable::Expire(int64_t nowMs)
{
	for (auto it = fPeers.begin(); it != fPeers.end();) {
		if (it->second.peer.expiresAtMs <= nowMs) {
			if (it->second.announced && fObserver != nullptr)
				fObserver->PeerLost(it->second.peer);
			it = fPeers.erase(it);
		} else {
			++it;
		}
	}
}

std::vector<Peer> PeerTable::Peers() const
{
	std::vector<Peer> out;
	for (const auto& kv : fPeers) {
		if (kv.second.announced)
			out.push_back(kv.second.peer);
	}
	return out;
}

} // namespace mdns
} // namespace bricola
} // namespace campiello
