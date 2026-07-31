// Responder.cpp
//
// See Responder.h.

#include "Responder.h"

#include <utility>
#include <vector>

#include "MdnsWire.h"

namespace campiello {
namespace bricola {
namespace mdns {

namespace {

// DNS-SD TTLs (seconds), the widely-used defaults: the shared PTR/TXT live long, the
// node-specific SRV/A shorter so a moved or renamed host ages out quickly.
const uint32_t kPtrTtl  = 4500;
const uint32_t kTxtTtl  = 4500;
const uint32_t kSrvTtl  = 120;
const uint32_t kHostTtl = 120;

// qtype for "any record" (RFC 1035): a browser may ask ANY instead of PTR.
const uint16_t kTypeAny = 255;

} // namespace

Responder::Responder(ServiceInfo self, std::string serviceName)
	:
	fInfo(std::move(self)),
	fService(std::move(serviceName))
{
}

std::string Responder::InstanceFqdn() const
{
	return fInfo.instance + "." + fService;
}

std::string Responder::AnnouncePacket() const
{
	const std::string instance = InstanceFqdn();

	std::vector<OutRecord> answers;
	OutRecord ptr;
	ptr.name = fService;
	ptr.type = kTypePTR;
	ptr.ttl = kPtrTtl;
	ptr.rdata = MakePtr(instance);
	answers.push_back(ptr);

	std::vector<OutRecord> additionals;

	OutRecord srv;
	srv.name = instance;
	srv.type = kTypeSRV;
	srv.ttl = kSrvTtl;
	srv.cacheFlush = true;
	srv.rdata = MakeSrv(0, 0, fInfo.port, fInfo.hostname);
	additionals.push_back(srv);

	// TXT keys in a stable order (PROPOSAL.md section 9). caps/fp are advisory and omitted
	// when empty; v/node/port/bfs are always present.
	std::vector<std::pair<std::string, std::string>> kv;
	kv.push_back({"v", std::to_string(fInfo.protocolVersion)});
	kv.push_back({"node", fInfo.instance});
	kv.push_back({"port", std::to_string(fInfo.port)});
	kv.push_back({"bfs", fInfo.bfsAttrs ? "1" : "0"});
	if (!fInfo.caps.empty())
		kv.push_back({"caps", fInfo.caps});
	if (!fInfo.fingerprintHex.empty())
		kv.push_back({"fp", fInfo.fingerprintHex});
	OutRecord txt;
	txt.name = instance;
	txt.type = kTypeTXT;
	txt.ttl = kTxtTtl;
	txt.cacheFlush = true;
	txt.rdata = MakeTxt(kv);
	additionals.push_back(txt);

	if (!fInfo.address.empty()) {
		OutRecord a;
		a.name = fInfo.hostname;
		a.type = kTypeA;
		a.ttl = kHostTtl;
		a.cacheFlush = true;
		a.rdata = MakeA(fInfo.address);
		if (!a.rdata.empty())
			additionals.push_back(a);
	}

	return BuildResponse(answers, additionals);
}

std::string Responder::GoodbyePacket() const
{
	std::vector<OutRecord> answers;
	OutRecord ptr;
	ptr.name = fService;
	ptr.type = kTypePTR;
	ptr.ttl = 0;   // goodbye
	ptr.rdata = MakePtr(InstanceFqdn());
	answers.push_back(ptr);
	return BuildResponse(answers, {});
}

std::string Responder::ResponseTo(const uint8_t* buf, size_t len) const
{
	if (fInfo.port == 0)
		return std::string();   // nothing to advertise yet

	Message msg;
	if (!Parse(buf, len, msg))
		return std::string();
	if (IsResponse(msg.flags))
		return std::string();   // only answer queries

	for (const Question& q : msg.questions) {
		if (q.name != fService)
			continue;
		if (q.qtype != kTypePTR && q.qtype != kTypeAny)
			continue;
		if ((q.qclass & 0x7FFF) != kClassIN)
			continue;
		return AnnouncePacket();
	}
	return std::string();
}

} // namespace mdns
} // namespace bricola
} // namespace campiello
