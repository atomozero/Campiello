// TrustStore.cpp
//
// Implementation of the paired-peer store. See TrustStore.h.

#include "TrustStore.h"

#include <cstdio>
#include <fstream>

#include <sys/stat.h>

namespace campiello {
namespace net {

void TrustStore::Pin(const Fingerprint& fingerprint, const std::string& name)
{
	for (PinnedPeer& peer : fPeers) {
		if (peer.fingerprint == fingerprint) {
			peer.name = name;
			return;
		}
	}
	fPeers.push_back(PinnedPeer{fingerprint, name});
}

bool TrustStore::Forget(const Fingerprint& fingerprint)
{
	for (size_t i = 0; i < fPeers.size(); i++) {
		if (fPeers[i].fingerprint == fingerprint) {
			fPeers.erase(fPeers.begin() + i);
			return true;
		}
	}
	return false;
}

const PinnedPeer* TrustStore::Find(const Fingerprint& fingerprint) const
{
	for (const PinnedPeer& peer : fPeers) {
		if (peer.fingerprint == fingerprint)
			return &peer;
	}
	return nullptr;
}

bool TrustStore::IsTrusted(const Fingerprint& fingerprint) const
{
	return Find(fingerprint) != nullptr;
}

TrustDecision TrustStore::Evaluate(const Fingerprint& fingerprint,
	const std::string& claimedName) const
{
	if (Find(fingerprint) != nullptr)
		return TrustDecision::kTrusted;
	for (const PinnedPeer& peer : fPeers) {
		if (peer.name == claimedName)
			return TrustDecision::kKeyChanged; // known name, different key
	}
	return TrustDecision::kUnknown;
}

bool TrustStore::SaveToFile(const std::string& path) const
{
	std::string tmp = path + ".tmp";
	{
		std::ofstream out(tmp, std::ios::out | std::ios::trunc);
		if (!out)
			return false;
		for (const PinnedPeer& peer : fPeers) {
			std::string name = peer.name;
			for (char& c : name) {
				if (c == '\n' || c == '\r')
					c = ' '; // names are single-line in the store
			}
			out << ToHex(peer.fingerprint) << ' ' << name << '\n';
		}
		out.flush();
		if (!out)
			return false;
	}
	::chmod(tmp.c_str(), 0600);
	return std::rename(tmp.c_str(), path.c_str()) == 0;
}

bool TrustStore::LoadFromFile(const std::string& path)
{
	std::ifstream in(path);
	if (!in)
		return false;

	std::vector<PinnedPeer> peers;
	std::string line;
	while (std::getline(in, line)) {
		if (!line.empty() && line.back() == '\r')
			line.pop_back();
		if (line.size() < 64)
			continue;
		Fingerprint fp;
		if (!FromHex(line.substr(0, 64), fp))
			continue;
		std::string name;
		if (line.size() > 65 && line[64] == ' ')
			name = line.substr(65);
		peers.push_back(PinnedPeer{fp, name});
	}
	fPeers = std::move(peers);
	return true;
}

} // namespace net
} // namespace campiello
