// Browser.cpp
//
// See Browser.h.

#include "Browser.h"

#include <utility>

#include "MdnsWire.h"

namespace campiello {
namespace bricola {
namespace mdns {

const char* const kCampielloService = "_campiello._tcp.local";

Browser::Browser(PeerObserver* observer, std::string serviceName)
	:
	fService(std::move(serviceName)),
	fTable(fService, observer)
{
}

std::string Browser::QueryPacket() const
{
	return BuildQuery(fService);
}

void Browser::OnPacket(const uint8_t* buf, size_t len, const std::string& srcIp, int64_t nowMs)
{
	fTable.Ingest(buf, len, srcIp, nowMs);
}

void Browser::Tick(int64_t nowMs)
{
	fTable.Expire(nowMs);
}

} // namespace mdns
} // namespace bricola
} // namespace campiello
