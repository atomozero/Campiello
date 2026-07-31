// SpotifyProbe.h
//
// Read-only probe for a Spotify Connect receiver discovered via _spotify-connect._tcp. A Connect
// receiver runs a small HTTP server (the "zeroconf" endpoint) whose unauthenticated `getInfo` action
// returns public device info (name, brand/model, device type, version, active user). The endpoint
// path is the CPath value from the mDNS TXT.
//
// This does NOT control playback: transferring/controlling playback needs a Spotify Premium account
// and the OAuth Web API (or the closed Connect protocol with an encrypted auth blob), a documented
// follow-up (docs/addons/spotify.md). The getInfo probe needs no account.
//
// Plain HTTP + a tiny JSON extractor: no third-party dependency (MIT-clean), links only libbe + the
// network kit.
//
// References: Spotify "commercial hardware" ZeroConf API (getInfo/addUser) and the librespot project.

#ifndef CAMPIELLO_SPOTIFY_SPOTIFYPROBE_H
#define CAMPIELLO_SPOTIFY_SPOTIFYPROBE_H

#include <string>

namespace campiello {
namespace spotify {

struct Info {
	std::string remoteName;       // the speaker's display name
	std::string brandDisplayName;
	std::string modelDisplayName;
	std::string deviceType;       // e.g. SPEAKER, AVR, TV, COMPUTER
	std::string version;
	std::string activeUser;       // the logged-in account, if any
};

class SpotifyProbe {
public:
	SpotifyProbe(const std::string& host, int port, const std::string& cpath = "/")
		: fHost(host), fPort(port), fCPath(cpath.empty() ? "/" : cpath) {}

	// GET <cpath>?action=getInfo and parse the reply. `okOut` reports HTTP success.
	Info GetInfo(bool* okOut);

private:
	std::string HttpGet(const std::string& path, int* status);

	std::string fHost;
	int         fPort;
	std::string fCPath;
};

// Value of "key":"value" in a JSON object (first occurrence, handles escaped quotes). Empty if absent.
std::string JsonString(const std::string& json, const std::string& key);

} // namespace spotify
} // namespace campiello

#endif // CAMPIELLO_SPOTIFY_SPOTIFYPROBE_H
