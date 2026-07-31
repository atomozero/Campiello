// SpotifyProbe.cpp
//
// See SpotifyProbe.h. Hand-rolled getInfo probe over plain HTTP.

#include "SpotifyProbe.h"

#include <cstdio>
#include <cstring>

#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>

namespace campiello {
namespace spotify {

std::string JsonString(const std::string& json, const std::string& key)
{
	std::string needle = "\"" + key + "\"";
	size_t k = json.find(needle);
	if (k == std::string::npos)
		return "";
	size_t colon = json.find(':', k + needle.size());
	if (colon == std::string::npos)
		return "";
	size_t q1 = json.find('"', colon + 1);
	if (q1 == std::string::npos)
		return "";
	std::string out;
	for (size_t i = q1 + 1; i < json.size(); ++i) {
		char c = json[i];
		if (c == '\\' && i + 1 < json.size()) { out += json[i + 1]; ++i; continue; }
		if (c == '"')
			break;
		out += c;
	}
	return out;
}

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

std::string SpotifyProbe::HttpGet(const std::string& path, int* status)
{
	*status = 0;
	int fd = TcpConnect(fHost, fPort);
	if (fd < 0)
		return "";

	std::string req = "GET " + path + " HTTP/1.1\r\n";
	req += "Host: " + fHost + ":" + std::to_string(fPort) + "\r\n";
	req += "User-Agent: Campiello/1.0\r\n";
	req += "Connection: close\r\n\r\n";

	if (write(fd, req.data(), req.size()) < 0) { close(fd); return ""; }

	std::string resp;
	char buf[4096];
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
	std::string body = resp.substr(hdrEnd + 4);
	// The endpoint may use chunked encoding; the payload is JSON, so trim to the outer braces.
	size_t j = body.find('{');
	size_t e = body.rfind('}');
	if (j != std::string::npos && e != std::string::npos && e >= j)
		return body.substr(j, e - j + 1);
	return body;
}

Info SpotifyProbe::GetInfo(bool* okOut)
{
	*okOut = false;
	Info info;
	std::string path = fCPath;
	if (path.empty() || path[0] != '/')
		path = "/" + path;
	path += (path.find('?') == std::string::npos) ? "?action=getInfo" : "&action=getInfo";

	int st = 0;
	std::string body = HttpGet(path, &st);
	if (st != 200 || body.empty())
		return info;

	*okOut = true;
	info.remoteName       = JsonString(body, "remoteName");
	info.brandDisplayName = JsonString(body, "brandDisplayName");
	info.modelDisplayName = JsonString(body, "modelDisplayName");
	info.deviceType       = JsonString(body, "deviceType");
	info.version          = JsonString(body, "version");
	info.activeUser       = JsonString(body, "activeUser");
	return info;
}

} // namespace spotify
} // namespace campiello
