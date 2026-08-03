// NetworkDirectory.cpp
//
// See NetworkDirectory.h.

#include "NetworkDirectory.h"

#include <algorithm>

#include "../bricola/mdns/RadarLabels.h"

namespace campiello {
namespace vicinato {

using bricola::mdns::RadarInstance;
using bricola::mdns::RadarSnapshot;

NetworkService ClassifyServiceType(const std::string& serviceType)
{
	NetworkService s;
	s.serviceType = serviceType;

	if (serviceType == "_campiello._tcp.local") {
		s.kind = ServiceKind::Campiello;
		s.auth = AuthKind::Pairing;
		s.backend = BackendKind::Cnp;
		s.browsable = true;
	} else if (serviceType == "_smb._tcp.local") {
		s.kind = ServiceKind::Smb;
		s.auth = AuthKind::Password;
		s.backend = BackendKind::Smb;
		s.browsable = true;
	} else if (serviceType == "_sftp-ssh._tcp.local" || serviceType == "_ssh._tcp.local") {
		s.kind = ServiceKind::Sftp;
		s.auth = AuthKind::Password;
		s.backend = BackendKind::Sftp;
		s.browsable = true;
	} else if (serviceType == "_workstation._tcp.local" || serviceType == "_device-info._tcp.local"
		|| serviceType == "_companion-link._tcp.local") {
		// A general computer on the LAN. _workstation is the classic "I am a machine" record
		// (avahi/macOS); _device-info is a host sidecar; _companion-link is Apple Continuity.
		s.kind = ServiceKind::Computer;
	} else if (serviceType == "_hue._tcp.local" || serviceType == "_hap._tcp.local"
		|| serviceType == "_matter._tcp.local" || serviceType == "_matterc._udp.local"
		|| serviceType == "_matterd._udp.local"
		|| serviceType == "_homekit._tcp.local" || serviceType == "_sleap._tcp.local"
		|| serviceType == "_dkapi._tcp.local" || serviceType == "_esphomelib._tcp.local") {
		// Home/IoT: Daikin air conditioners (_dkapi) and ESPHome nodes (_esphomelib) both expose a
		// local HTTP UI, so they group with the home devices and get the "open web UI" action.
		s.kind = ServiceKind::Home;
	} else if (serviceType == "_http._tcp.local" || serviceType == "_https._tcp.local") {
		s.kind = ServiceKind::Web;
	} else if (serviceType == "_ipp._tcp.local" || serviceType == "_ipps._tcp.local"
		|| serviceType == "_printer._tcp.local" || serviceType == "_pdl-datastream._tcp.local"
		|| serviceType == "_scanner._tcp.local" || serviceType == "_uscan._tcp.local") {
		s.kind = ServiceKind::Printer;
	} else if (serviceType == "_airplay._tcp.local" || serviceType == "_raop._tcp.local"
		|| serviceType == "_googlecast._tcp.local" || serviceType == "_spotify-connect._tcp.local"
		|| serviceType == "_amzn-wplay._tcp.local" || serviceType == "_daap._tcp.local") {
		s.kind = ServiceKind::Media;
	} else {
		s.kind = ServiceKind::Other;
	}
	// Non-browsable kinds keep backend None / auth None.
	return s;
}

std::vector<NetworkService> BuildNeighborhood(const RadarSnapshot& snap)
{
	std::vector<NetworkService> out;
	out.reserve(snap.instances.size());

	auto endsWith = [](const std::string& s, const std::string& suf) {
		return s.size() >= suf.size() && s.compare(s.size() - suf.size(), suf.size(), suf) == 0;
	};

	for (const RadarInstance& inst : snap.instances) {
		// Skip mDNS reverse-address PTR pseudo-services (x.x.x.x.in-addr.arpa / ...ip6.arpa):
		// they are name lookups, not browsable services.
		if (endsWith(inst.type, ".in-addr.arpa") || endsWith(inst.type, ".ip6.arpa")
			|| endsWith(inst.name, ".in-addr.arpa") || endsWith(inst.name, ".ip6.arpa"))
			continue;

		NetworkService s = ClassifyServiceType(inst.type);
		s.id = inst.type + "/" + inst.name;
		s.host = inst.addrs.empty() ? inst.host : inst.addrs[0];
		s.port = inst.port;
		s.txt = inst.txt;

		// Prefer the decoded instance summary as the label; fall back to the service label + host.
		std::string summary = bricola::mdns::InstanceSummary(inst.type, inst.txt);
		bricola::mdns::ServiceInfo si = bricola::mdns::LookupService(inst.type);
		s.category = si.category;
		if (!summary.empty())
			s.label = summary;
		else if (!s.host.empty())
			s.label = si.label + " (" + s.host + ")";
		else
			s.label = si.label;

		out.push_back(std::move(s));
	}

	// A machine that also offers a real service (a share, SSH, a web UI...) is already represented by
	// that service. Keep a Computer entry only for hosts that would otherwise not appear at all, so
	// "computers" surfaces the machines nothing else covers rather than duplicating them.
	std::vector<std::string> served;
	for (const NetworkService& s : out)
		if (s.kind != ServiceKind::Computer && !s.host.empty())
			served.push_back(s.host);
	out.erase(std::remove_if(out.begin(), out.end(), [&](const NetworkService& s) {
		if (s.kind != ServiceKind::Computer || s.host.empty())
			return false;
		return std::find(served.begin(), served.end(), s.host) != served.end();
	}), out.end());

	std::sort(out.begin(), out.end(), [](const NetworkService& a, const NetworkService& b) {
		if (a.kind != b.kind)
			return static_cast<int>(a.kind) < static_cast<int>(b.kind);
		return a.label < b.label;
	});
	return out;
}

} // namespace vicinato
} // namespace campiello
