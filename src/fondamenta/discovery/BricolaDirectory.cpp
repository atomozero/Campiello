// BricolaDirectory.cpp
//
// See BricolaDirectory.h.

#include "BricolaDirectory.h"

namespace campiello {
namespace fondamenta {

std::string BricolaDirectory::SanitizeName(const std::string& instance)
{
	std::string out;
	for (char c : instance) {
		unsigned char u = static_cast<unsigned char>(c);
		bool ok = (u >= 'a' && u <= 'z') || (u >= 'A' && u <= 'Z')
			|| (u >= '0' && u <= '9') || c == '-' || c == '_' || c == '.' || c == ' ';
		out += ok ? c : '-';
	}
	while (!out.empty() && out.front() == '.')
		out.erase(out.begin()); // no leading dot (would be a hidden entry)
	while (!out.empty() && out.back() == ' ')
		out.pop_back();
	return out;
}

std::vector<PeerEndpoint> BricolaDirectory::Endpoints()
{
	std::vector<PeerEndpoint> out;
	for (const bricola::mdns::Peer& peer : fBricola.Peers()) {
		if (peer.addresses.empty() || peer.port == 0)
			continue; // not yet resolved / not connectable
		std::string name = SanitizeName(peer.instance);
		if (name.empty())
			continue;
		out.push_back(PeerEndpoint{name, peer.addresses.front(), peer.port});
	}
	return out;
}

} // namespace fondamenta
} // namespace campiello
