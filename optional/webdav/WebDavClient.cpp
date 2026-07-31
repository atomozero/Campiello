// WebDavClient.cpp
//
// See WebDavClient.h. Hand-rolled WebDAV over plain HTTP with a small multistatus XML parser.

#include "WebDavClient.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>

namespace campiello {
namespace webdav {

namespace xml {

// Content start (index just after the '>') of the next opening <...tag> at or after `from`.
static size_t NextOpen(const std::string& doc, const std::string& tag, size_t from)
{
	std::string needle = tag + ">";
	size_t p = from;
	while ((p = doc.find(needle, p)) != std::string::npos) {
		size_t lt = doc.rfind('<', p);
		if (lt != std::string::npos) {
			std::string opener = doc.substr(lt, p + needle.size() - lt);
			if (opener.find(' ') == std::string::npos && opener.compare(0, 2, "</") != 0)
				return p + needle.size();
		}
		p += needle.size();
	}
	return std::string::npos;
}

std::vector<std::string> Blocks(const std::string& doc, const std::string& tag)
{
	std::vector<std::string> out;
	std::string needle = tag + ">";
	size_t i = 0;
	while (true) {
		size_t cs = NextOpen(doc, tag, i);
		if (cs == std::string::npos)
			break;
		// find the matching closing tag: the next "<.../tag>" whose opener starts with "</"
		// (the tag carries a namespace prefix, e.g. "</D:response>").
		size_t p = cs, closeLt = std::string::npos;
		while ((p = doc.find(needle, p)) != std::string::npos) {
			size_t lt = doc.rfind('<', p);
			if (lt != std::string::npos && doc.compare(lt, 2, "</") == 0) { closeLt = lt; break; }
			p += needle.size();
		}
		if (closeLt == std::string::npos)
			break;
		out.push_back(doc.substr(cs, closeLt - cs));
		i = closeLt + needle.size();
	}
	return out;
}

std::string Tag(const std::string& doc, const std::string& tag)
{
	size_t cs = NextOpen(doc, tag, 0);
	if (cs == std::string::npos)
		return "";
	size_t end = doc.find('<', cs);
	if (end == std::string::npos)
		return "";
	return doc.substr(cs, end - cs);
}

std::string UrlDecode(const std::string& s)
{
	std::string out;
	for (size_t i = 0; i < s.size(); ++i) {
		if (s[i] == '%' && i + 2 < s.size()) {
			auto hex = [](char c) -> int {
				if (c >= '0' && c <= '9') return c - '0';
				if (c >= 'a' && c <= 'f') return c - 'a' + 10;
				if (c >= 'A' && c <= 'F') return c - 'A' + 10;
				return -1;
			};
			int hi = hex(s[i + 1]), lo = hex(s[i + 2]);
			if (hi >= 0 && lo >= 0) { out += (char)(hi * 16 + lo); i += 2; continue; }
		}
		out += s[i];
	}
	return out;
}

static std::string LeafOf(const std::string& href)
{
	std::string p = href;
	if (!p.empty() && p.back() == '/')
		p.pop_back();
	size_t slash = p.rfind('/');
	return (slash == std::string::npos) ? p : p.substr(slash + 1);
}

std::vector<Resource> ParseMultistatus(const std::string& body, const std::string& selfPath)
{
	std::vector<Resource> out;
	for (const std::string& resp : Blocks(body, "response")) {
		Resource r;
		r.href = UrlDecode(Tag(resp, "href"));
		if (r.href.empty())
			continue;
		// the collection's own entry: href equals the requested path (with/without trailing slash)
		std::string a = r.href, b = selfPath;
		if (!a.empty() && a.back() == '/') a.pop_back();
		if (!b.empty() && b.back() == '/') b.pop_back();
		if (a == b)
			continue;

		size_t rt = resp.find("resourcetype");
		size_t coll = resp.find("collection");
		r.isDir = (rt != std::string::npos && coll != std::string::npos && coll >= rt
			&& coll < rt + 80);

		std::string dn = Tag(resp, "displayname");
		r.name = !dn.empty() ? dn : LeafOf(r.href);

		std::string len = Tag(resp, "getcontentlength");
		if (!len.empty())
			r.size = std::atoll(len.c_str());

		out.push_back(r);
	}
	return out;
}

} // namespace xml

namespace {
std::string Base64(const std::string& in)
{
	static const char* t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	std::string out;
	int val = 0, bits = -6;
	for (unsigned char c : in) {
		val = (val << 8) + c;
		bits += 8;
		while (bits >= 0) { out += t[(val >> bits) & 0x3f]; bits -= 6; }
	}
	if (bits > -6) out += t[((val << 8) >> (bits + 8)) & 0x3f];
	while (out.size() % 4) out += '=';
	return out;
}

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

std::string WebDavClient::HttpRequest(const std::string& method, const std::string& path, int depth,
	const std::string& body, int* status)
{
	*status = 0;
	int fd = TcpConnect(fHost, fPort);
	if (fd < 0)
		return "";

	std::string req = method + " " + path + " HTTP/1.1\r\n";
	req += "Host: " + fHost + ":" + std::to_string(fPort) + "\r\n";
	if (depth >= 0)
		req += "Depth: " + std::to_string(depth) + "\r\n";
	if (!fUser.empty())
		req += "Authorization: Basic " + Base64(fUser + ":" + fPass) + "\r\n";
	if (!body.empty()) {
		req += "Content-Type: text/xml; charset=\"utf-8\"\r\n";
		req += "Content-Length: " + std::to_string(body.size()) + "\r\n";
	}
	req += "Connection: close\r\n\r\n";
	req += body;

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

std::vector<Resource> WebDavClient::List(const std::string& path, bool* okOut)
{
	*okOut = false;
	std::string reqPath = path.empty() ? "/" : path;
	std::string propfind =
		"<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
		"<D:propfind xmlns:D=\"DAV:\"><D:prop>"
		"<D:displayname/><D:getcontentlength/><D:resourcetype/>"
		"</D:prop></D:propfind>";
	int st = 0;
	std::string body = HttpRequest("PROPFIND", reqPath, 1, propfind, &st);
	if (st != 207)
		return {};
	*okOut = true;
	return xml::ParseMultistatus(body, reqPath);
}

bool WebDavClient::Download(const std::string& href, const std::string& localPath)
{
	int st = 0;
	std::string body = HttpRequest("GET", href, -1, "", &st);
	if (st != 200)
		return false;
	FILE* f = std::fopen(localPath.c_str(), "wb");
	if (f == nullptr)
		return false;
	std::fwrite(body.data(), 1, body.size(), f);
	std::fclose(f);
	return true;
}

} // namespace webdav
} // namespace campiello
