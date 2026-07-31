// DaapClient.cpp
//
// See DaapClient.h. Hand-rolled DAAP over plain HTTP with a small DMAP (TLV) parser.

#include "DaapClient.h"

#include <cstdio>
#include <cstring>

#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>

namespace campiello {
namespace daap {

namespace dmap {

namespace {
uint32_t BE32(const std::string& b, size_t off)
{
	return ((uint8_t)b[off] << 24) | ((uint8_t)b[off + 1] << 16)
		| ((uint8_t)b[off + 2] << 8) | (uint8_t)b[off + 3];
}

// A DMAP element's value is itself a container if it starts with a plausible child element: 4
// lowercase-letter code + a length that fits. This lets us walk the tree without a full content-code
// table (dmap-parser uses the code dictionary; the heuristic is enough for our read-only browsing).
bool LooksLikeContainer(const std::string& v)
{
	if (v.size() < 8)
		return false;
	for (int i = 0; i < 4; ++i)
		if (v[i] < 'a' || v[i] > 'z')
			return false;
	uint32_t innerLen = BE32(v, 4);
	return innerLen <= v.size() - 8;
}
} // namespace

long long AsInt(const std::string& value)
{
	long long n = 0;
	for (unsigned char c : value)
		n = (n << 8) | c;
	return n;
}

// Depth-first search for the first element whose code matches, recursing into containers.
static bool FindLeafIn(const std::string& buf, size_t start, size_t end, const char* code,
	std::string& out)
{
	size_t i = start;
	while (i + 8 <= end) {
		std::string c = buf.substr(i, 4);
		uint32_t len = BE32(buf, i + 4);
		size_t valStart = i + 8;
		if (valStart + len > end)
			break;
		std::string value = buf.substr(valStart, len);
		if (c == code) {
			out = value;
			return true;
		}
		if (LooksLikeContainer(value) && FindLeafIn(value, 0, value.size(), code, out))
			return true;
		i = valStart + len;
	}
	return false;
}

std::string FindLeaf(const std::string& buf, const char* code, bool* found)
{
	std::string out;
	bool f = FindLeafIn(buf, 0, buf.size(), code, out);
	if (found != nullptr)
		*found = f;
	return out;
}

// Collect every "mlit" record's direct string leaves into a Track.
static void CollectTracks(const std::string& buf, size_t start, size_t end, std::vector<Track>& out)
{
	size_t i = start;
	while (i + 8 <= end) {
		std::string c = buf.substr(i, 4);
		uint32_t len = BE32(buf, i + 4);
		size_t valStart = i + 8;
		if (valStart + len > end)
			break;
		std::string value = buf.substr(valStart, len);
		if (c == "mlit") {
			Track t;
			bool f = false;
			t.title  = FindLeaf(value, "minm", &f);
			t.artist = FindLeaf(value, "asar", &f);
			t.album  = FindLeaf(value, "asal", &f);
			if (!t.title.empty() || !t.artist.empty())
				out.push_back(t);
		} else if (LooksLikeContainer(value)) {
			CollectTracks(value, 0, value.size(), out);
		}
		i = valStart + len;
	}
}

std::vector<Track> ParseTracks(const std::string& buf)
{
	std::vector<Track> out;
	CollectTracks(buf, 0, buf.size(), out);
	return out;
}

} // namespace dmap

namespace {
int TcpConnect(const std::string& host, int port)
{
	char portStr[8];
	std::snprintf(portStr, sizeof(portStr), "%d", port);
	struct addrinfo hints;
	std::memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	struct addrinfo* res = nullptr;
	if (getaddrinfo(host.c_str(), portStr, &hints, &res) != 0 || res == nullptr)
		return -1;
	int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
	if (fd < 0) { freeaddrinfo(res); return -1; }
	if (connect(fd, res->ai_addr, res->ai_addrlen) != 0) {
		close(fd); freeaddrinfo(res); return -1;
	}
	freeaddrinfo(res);
	return fd;
}
} // namespace

std::string DaapClient::HttpGet(const std::string& path, int* status)
{
	*status = 0;
	int fd = TcpConnect(fHost, fPort);
	if (fd < 0)
		return "";

	std::string req = "GET " + path + " HTTP/1.1\r\n";
	req += "Host: " + fHost + ":" + std::to_string(fPort) + "\r\n";
	req += "Client-DAAP-Version: 3.0\r\n";
	req += "User-Agent: Campiello/1.0\r\n";
	req += "Viewer-Only-Client: 1\r\n";
	req += "Accept-Encoding: identity\r\n"; // no gzip: we parse the raw DMAP bytes
	req += "Connection: close\r\n\r\n";

	if (write(fd, req.data(), req.size()) < 0) { close(fd); return ""; }

	std::string resp;
	char buf[8192];
	ssize_t n;
	while ((n = read(fd, buf, sizeof(buf))) > 0)
		resp.append(buf, n);
	close(fd);

	size_t sp = resp.find(' ');
	if (sp != std::string::npos)
		*status = std::atoi(resp.c_str() + sp + 1);
	size_t hdrEnd = resp.find("\r\n\r\n");
	if (hdrEnd == std::string::npos)
		return "";
	return resp.substr(hdrEnd + 4);
}

int DaapClient::Login()
{
	int st = 0;
	std::string body = HttpGet("/login", &st);
	if (st != 200)
		return 0;
	bool found = false;
	std::string mlid = dmap::FindLeaf(body, "mlid", &found);
	return found ? (int)dmap::AsInt(mlid) : 0;
}

int DaapClient::FirstDatabaseId(int sessionId)
{
	int st = 0;
	std::string body = HttpGet("/databases?session-id=" + std::to_string(sessionId), &st);
	if (st != 200)
		return 0;
	bool found = false;
	std::string miid = dmap::FindLeaf(body, "miid", &found);
	return found ? (int)dmap::AsInt(miid) : 0;
}

std::vector<Track> DaapClient::ListTracks(int max, bool* okOut)
{
	*okOut = false;
	int session = Login();
	if (session == 0)
		return {};
	int db = FirstDatabaseId(session);
	if (db == 0)
		return {};
	std::string path = "/databases/" + std::to_string(db) + "/items?session-id="
		+ std::to_string(session)
		+ "&meta=dmap.itemid,dmap.itemname,daap.songartist,daap.songalbum&type=music";
	int st = 0;
	std::string body = HttpGet(path, &st);
	if (st != 200)
		return {};
	*okOut = true;
	std::vector<Track> tracks = dmap::ParseTracks(body);
	if (max > 0 && (int)tracks.size() > max)
		tracks.resize(max);
	return tracks;
}

} // namespace daap
} // namespace campiello
