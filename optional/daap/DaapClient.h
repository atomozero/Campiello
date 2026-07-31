// DaapClient.h
//
// Minimal DAAP (Digital Audio Access Protocol) client for a shared iTunes/OwnTone music library
// discovered via _daap._tcp. DAAP is HTTP on port 3689 whose responses are DMAP: a binary
// type-length-value tree (4-byte ASCII content code, 4-byte big-endian length, value). Flow:
//   GET /login                          -> mlog { mlid = session-id }
//   GET /databases?session-id=N         -> avdb { ... mlit { miid = database id } }
//   GET /databases/<id>/items?session-id=N&meta=... -> adbs { ... mlcl { mlit { minm, asar, asal } } }
//
// This lists the library's tracks (title/artist/album). Streaming/playback of a track is a documented
// follow-up (docs/addons/daap.md). Plain HTTP + a hand-rolled DMAP parser: no third-party dependency
// (MIT-clean), links only libbe + the network kit.
//
// References: github.com/bjoernricks/daap-protocol (protocol docs) and github.com/mattstevens/
// dmap-parser (the TLV layout and the minm/asar/asal/miid/mlid content codes).

#ifndef CAMPIELLO_DAAP_DAAPCLIENT_H
#define CAMPIELLO_DAAP_DAAPCLIENT_H

#include <string>
#include <vector>

namespace campiello {
namespace daap {

struct Track {
	std::string title;   // minm
	std::string artist;  // asar
	std::string album;   // asal
};

class DaapClient {
public:
	explicit DaapClient(const std::string& host, int port = 3689) : fHost(host), fPort(port) {}

	// Log in, find the first database, and list up to `max` tracks. `okOut` reports success.
	std::vector<Track> ListTracks(int max, bool* okOut);

private:
	int  Login();                                   // returns a session-id, or 0 on failure
	int  FirstDatabaseId(int sessionId);            // the miid of the first database, or 0
	std::string HttpGet(const std::string& path, int* status);

	std::string fHost;
	int         fPort;
};

// DMAP parsing (dependency-free; exposed for testing).
namespace dmap {
	// Raw value bytes of the first element with `code` anywhere in the tree (recursing into
	// containers). Empty if absent. `found` distinguishes "absent" from "present but empty".
	std::string FindLeaf(const std::string& buf, const char* code, bool* found);
	// Interpret a big-endian value of 1/2/4/8 bytes as an integer.
	long long AsInt(const std::string& value);
	// Extract the tracks from an items response: one Track per "mlit" record (title/artist/album).
	std::vector<Track> ParseTracks(const std::string& buf);
}

} // namespace daap
} // namespace campiello

#endif // CAMPIELLO_DAAP_DAAPCLIENT_H
