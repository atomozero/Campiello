// MdnsRadar.cpp
//
// See MdnsRadar.h.

#include "MdnsRadar.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>

#include "MdnsWire.h"

namespace campiello {
namespace bricola {
namespace mdns {

namespace {

// The DNS-SD service-type enumeration name (RFC 6763 section 9): a PTR query for it asks every
// responder to list the service types it offers.
const char* const kMetaQuery = "_services._dns-sd._udp.local";

std::string ToLower(std::string s)
{
	std::transform(s.begin(), s.end(), s.begin(),
		[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return s;
}

// Fold a DNS-SD subtype into its parent service (RFC 6763 section 7.1). A subtype has the form
// "<label>._sub.<parenttype>", e.g. "_printer._sub._http._tcp.local". Returns the parent type
// and sets `subtypeLabel` to the leading label; for a plain type, returns it unchanged with an
// empty subtypeLabel.
std::string NormalizeServiceType(const std::string& type, std::string& subtypeLabel)
{
	const std::string marker = "._sub.";
	size_t pos = type.find(marker);
	if (pos == std::string::npos) {
		subtypeLabel.clear();
		return type;
	}
	subtypeLabel = type.substr(0, pos);
	return type.substr(pos + marker.size());
}

// Record a subtype label on a service, once.
void AddSubtype(RadarService& svc, const std::string& label)
{
	if (label.empty())
		return;
	if (std::find(svc.subtypes.begin(), svc.subtypes.end(), label) == svc.subtypes.end())
		svc.subtypes.push_back(label);
}

} // namespace

MdnsRadar::~MdnsRadar()
{
	Stop();
}

int64_t MdnsRadar::NowMs()
{
	using namespace std::chrono;
	return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

std::string MdnsRadar::Interface() const
{
	std::lock_guard<std::mutex> lock(fMutex);
	return fInterface;
}

bool MdnsRadar::Start(const char* interfaceIpv4)
{
	if (IsRunning())
		return true;

	// Resolve the interface: explicit argument, else the env override, else the primary NIC.
	std::string iface;
	if (interfaceIpv4 != nullptr && interfaceIpv4[0] != '\0') {
		iface = interfaceIpv4;
	} else {
		const char* env = std::getenv("CAMPIELLO_MDNS_IFACE");
		iface = (env != nullptr && env[0] != '\0') ? std::string(env) : PrimaryMulticastIPv4();
	}

	if (!fSocket.Open(iface.empty() ? nullptr : iface.c_str())) {
		fError = fSocket.Error();
		return false;
	}

	{
		std::lock_guard<std::mutex> lock(fMutex);
		fInterface = iface;
		fStartMs = NowMs();
		fLastMs = fStartMs;
		fTotalPackets = fTotalRecords = fDroppedPackets = 0;
		fSources.clear();
		fServices.clear();
		fInstances.clear();
		fHostAddrs.clear();
	}

	fStop.store(false);
	fThread = std::thread(&MdnsRadar::Run, this);
	return true;
}

void MdnsRadar::Stop()
{
	if (!IsRunning())
		return;
	fStop.store(true);
	fThread.join();
	fSocket.Close();
}

void MdnsRadar::SendQueries()
{
	// The meta-query first, then a PTR query for each service type we already know, to pull in
	// their instances.
	std::string meta = BuildQuery(kMetaQuery);
	fSocket.SendMulticast(meta.data(), meta.size());

	std::vector<std::string> types;
	{
		std::lock_guard<std::mutex> lock(fMutex);
		types.reserve(fServices.size());
		for (const auto& kv : fServices)
			types.push_back(kv.first);
	}
	for (const std::string& type : types) {
		std::string q = BuildQuery(type);
		fSocket.SendMulticast(q.data(), q.size());
	}
}

void MdnsRadar::Run()
{
	SendQueries();
	int64_t lastQuery = NowMs();
	const int64_t kQueryIntervalMs = 4000;

	std::vector<uint8_t> buf(4096);
	while (!fStop.load()) {
		std::string srcIp;
		uint16_t srcPort = 0;
		int n = fSocket.Receive(buf.data(), buf.size(), 400, srcIp, srcPort);
		if (n > 0)
			Ingest(buf.data(), static_cast<size_t>(n), srcIp, NowMs());

		int64_t now = NowMs();
		if (fQueryNow.exchange(false) || now - lastQuery >= kQueryIntervalMs) {
			SendQueries();
			lastQuery = now;
		}
	}
}

void MdnsRadar::Ingest(const uint8_t* buf, size_t len, const std::string& srcIp, int64_t nowMs)
{
	std::lock_guard<std::mutex> lock(fMutex);

	fLastMs = nowMs;
	++fTotalPackets;

	RadarSource& src = fSources[srcIp];
	if (src.ip.empty()) {
		src.ip = srcIp;
		src.firstMs = nowMs;
	}
	++src.packets;
	src.lastMs = nowMs;

	Message msg;
	if (!Parse(buf, len, msg)) {
		++fDroppedPackets;
		return;
	}

	// Walk every record the packet carries (answers, additionals, authorities).
	auto handle = [&](const std::vector<Record>& records) {
		for (const Record& rec : records) {
			++fTotalRecords;
			++src.records;

			if (rec.type == kTypePTR) {
				std::string target;
				if (!DecodePtr(buf, len, rec, target) || target.empty())
					continue;
				if (rec.name == kMetaQuery) {
					// Service-type enumeration: the target is a service type, possibly a subtype
					// that we fold into its parent.
					std::string sub;
					std::string parent = NormalizeServiceType(target, sub);
					RadarService& svc = fServices[parent];
					svc.type = parent;
					svc.lastMs = nowMs;
					AddSubtype(svc, sub);
				} else {
					// A normal PTR: rec.name is the service type (or a subtype of it), target is
					// the instance.
					std::string sub;
					std::string parent = NormalizeServiceType(rec.name, sub);
					RadarService& svc = fServices[parent];
					svc.type = parent;
					svc.lastMs = nowMs;
					AddSubtype(svc, sub);
					RadarInstance& inst = fInstances[target];
					inst.name = target;
					inst.type = parent;
					if (inst.srcIp.empty())
						inst.srcIp = srcIp;
					inst.lastMs = nowMs;
				}
			} else if (rec.type == kTypeSRV) {
				uint16_t prio = 0, weight = 0, port = 0;
				std::string host;
				if (!DecodeSrv(buf, len, rec, prio, weight, port, host))
					continue;
				RadarInstance& inst = fInstances[rec.name];
				inst.name = rec.name;
				inst.host = host;
				inst.port = port;
				if (inst.srcIp.empty())
					inst.srcIp = srcIp;
				inst.lastMs = nowMs;
			} else if (rec.type == kTypeTXT) {
				std::vector<std::pair<std::string, std::string>> kv;
				if (!DecodeTxt(rec, kv))
					continue;
				RadarInstance& inst = fInstances[rec.name];
				inst.name = rec.name;
				inst.txt = std::move(kv);
				inst.lastMs = nowMs;
			} else if (rec.type == kTypeA) {
				std::string addr;
				if (!DecodeA(rec, addr))
					continue;
				fHostAddrs[ToLower(rec.name)].insert(addr);
			}
		}
	};
	handle(msg.answers);
	handle(msg.additionals);
	handle(msg.authorities);
}

RadarSnapshot MdnsRadar::Snapshot() const
{
	std::lock_guard<std::mutex> lock(fMutex);

	RadarSnapshot out;
	out.interfaceIp = fInterface;
	out.startMs = fStartMs;
	out.nowMs = fLastMs;
	out.totalPackets = fTotalPackets;
	out.totalRecords = fTotalRecords;
	out.droppedPackets = fDroppedPackets;

	out.sources.reserve(fSources.size());
	for (const auto& kv : fSources)
		out.sources.push_back(kv.second);
	std::sort(out.sources.begin(), out.sources.end(),
		[](const RadarSource& a, const RadarSource& b) { return a.ip < b.ip; });

	// Count instances per service type as we copy them, resolving each address.
	std::map<std::string, size_t> perType;
	out.instances.reserve(fInstances.size());
	for (const auto& kv : fInstances) {
		RadarInstance inst = kv.second;
		auto it = fHostAddrs.find(ToLower(inst.host));
		if (it != fHostAddrs.end())
			inst.addrs.assign(it->second.begin(), it->second.end());
		if (inst.addrs.empty() && !inst.srcIp.empty())
			inst.addrs.push_back(inst.srcIp); // fallback: the responder's own address
		if (!inst.type.empty())
			++perType[inst.type];
		out.instances.push_back(std::move(inst));
	}
	std::sort(out.instances.begin(), out.instances.end(),
		[](const RadarInstance& a, const RadarInstance& b) {
			if (a.type != b.type) return a.type < b.type;
			return a.name < b.name;
		});

	out.services.reserve(fServices.size());
	for (const auto& kv : fServices) {
		RadarService svc = kv.second;
		auto it = perType.find(svc.type);
		svc.instances = (it != perType.end()) ? it->second : 0;
		out.services.push_back(svc);
	}
	std::sort(out.services.begin(), out.services.end(),
		[](const RadarService& a, const RadarService& b) { return a.type < b.type; });

	return out;
}

} // namespace mdns
} // namespace bricola
} // namespace campiello
