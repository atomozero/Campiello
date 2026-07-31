// DialClient.cpp
//
// See DialClient.h. Hand-rolled DIAL over plain HTTP.

#include "DialClient.h"

#include <cctype>
#include <cstdio>
#include <cstring>

#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>

namespace campiello {
namespace cast {

// Value of the first opening element whose local name is `tag` (namespace-prefix agnostic), skipping
// closing tags and tags with attributes.
std::string XmlTag(const std::string& doc, const std::string& tag)
{
	std::string open = tag + ">";
	size_t pos = 0;
	while ((pos = doc.find(open, pos)) != std::string::npos) {
		size_t start = doc.rfind('<', pos);
		if (start != std::string::npos) {
			std::string opener = doc.substr(start, pos + open.size() - start);
			if (opener.find(' ') == std::string::npos && opener.compare(0, 2, "</") != 0) {
				size_t valStart = pos + open.size();
				size_t valEnd = doc.find('<', valStart);
				if (valEnd != std::string::npos)
					return doc.substr(valStart, valEnd - valStart);
			}
		}
		pos += open.size();
	}
	return "";
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

DialClient::Response DialClient::HttpRequest(const std::string& method, const std::string& path,
	const std::string& body)
{
	Response r;
	int fd = TcpConnect(fHost, fPort);
	if (fd < 0)
		return r;

	std::string req = method + " " + path + " HTTP/1.1\r\n";
	req += "Host: " + fHost + ":" + std::to_string(fPort) + "\r\n";
	if (!body.empty())
		req += "Content-Length: " + std::to_string(body.size()) + "\r\n";
	else if (method == "POST")
		req += "Content-Length: 0\r\n";
	req += "Connection: close\r\n\r\n";
	req += body;

	if (write(fd, req.data(), req.size()) < 0) { close(fd); return r; }

	std::string resp;
	char buf[4096];
	ssize_t n;
	while ((n = read(fd, buf, sizeof(buf))) > 0)
		resp.append(buf, n);
	close(fd);

	size_t sp = resp.find(' ');
	if (sp != std::string::npos)
		r.status = std::atoi(resp.c_str() + sp + 1);
	size_t hdrEnd = resp.find("\r\n\r\n");
	if (hdrEnd == std::string::npos)
		return r;
	std::string headers = resp.substr(0, hdrEnd);
	// case-insensitive "Location:"
	std::string lower = headers;
	for (char& c : lower) c = (char)tolower(c);
	size_t lp = lower.find("location:");
	if (lp != std::string::npos) {
		size_t vs = lp + 9;
		while (vs < headers.size() && (headers[vs] == ' ' || headers[vs] == '\t')) ++vs;
		size_t ve = headers.find("\r\n", vs);
		r.location = headers.substr(vs, ve == std::string::npos ? std::string::npos : ve - vs);
	}
	r.body = resp.substr(hdrEnd + 4);
	return r;
}

std::string DialClient::FriendlyName()
{
	Response r = HttpRequest("GET", "/ssdp/device-desc.xml", "");
	if (r.status != 200)
		return "";
	return XmlTag(r.body, "friendlyName");
}

std::string DialClient::AppState(const std::string& appName, bool* okOut)
{
	Response r = HttpRequest("GET", "/apps/" + appName, "");
	*okOut = (r.status == 200);
	if (r.status != 200)
		return "";
	return XmlTag(r.body, "state");
}

bool DialClient::Launch(const std::string& appName)
{
	Response r = HttpRequest("POST", "/apps/" + appName, "");
	return r.status == 200 || r.status == 201;
}

bool DialClient::Stop(const std::string& appName)
{
	Response r = HttpRequest("DELETE", "/apps/" + appName + "/run", "");
	return r.status == 200 || r.status == 204;
}

} // namespace cast
} // namespace campiello
