// EsclClient.cpp
//
// See EsclClient.h. Hand-rolled eSCL over plain HTTP.

#include "EsclClient.h"

#include <cctype>
#include <cstdio>
#include <cstring>

#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>

namespace campiello {
namespace escl {

namespace xml {

// Find the value of an element whose local name is `tag`, ignoring any namespace prefix. Matches
// "<...tag>value</...tag>" by locating "tag>" preceded by '<' or ':'.
std::string Tag(const std::string& doc, const std::string& tag)
{
	std::string open = tag + ">";
	size_t pos = 0;
	while ((pos = doc.find(open, pos)) != std::string::npos) {
		// the char before `tag` must be '<' or ':' (namespace separator) to be a real element open
		if (pos >= 1) {
			// walk back over the tag name to the '<' or ':'
			size_t start = doc.rfind('<', pos);
			if (start != std::string::npos) {
				std::string opener = doc.substr(start, pos + open.size() - start);
				// opener looks like "<pwg:MakeAndModel>" or "<MakeAndModel>"; accept an opening tag
				// with no attributes (no space) and not a closing tag ("</...>")
				if (opener.find(' ') == std::string::npos && opener.compare(0, 2, "</") != 0) {
					size_t valStart = pos + open.size();
					size_t valEnd = doc.find('<', valStart);
					if (valEnd != std::string::npos)
						return doc.substr(valStart, valEnd - valStart);
				}
			}
		}
		pos += open.size();
	}
	return "";
}

std::vector<std::string> Tags(const std::string& doc, const std::string& tag)
{
	std::vector<std::string> out;
	std::string open = tag + ">";
	size_t pos = 0;
	while ((pos = doc.find(open, pos)) != std::string::npos) {
		size_t start = doc.rfind('<', pos);
		size_t valStart = pos + open.size();
		size_t valEnd = doc.find('<', valStart);
		if (start != std::string::npos && valEnd != std::string::npos) {
			std::string opener = doc.substr(start, valStart - start);
			if (opener.find(' ') == std::string::npos && opener.compare(0, 2, "</") != 0)
				out.push_back(doc.substr(valStart, valEnd - valStart));
		}
		pos = valStart;
	}
	return out;
}

std::string BuildScanSettings(const ScanOptions& opt)
{
	char buf[1024];
	std::snprintf(buf, sizeof(buf),
		"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
		"<scan:ScanSettings xmlns:scan=\"http://schemas.hp.com/imaging/escl/2011/05/03\""
		" xmlns:pwg=\"http://www.pwg.org/schemas/2010/12/sm\">\n"
		" <pwg:Version>2.6</pwg:Version>\n"
		" <pwg:ScanRegions>\n"
		"  <pwg:ScanRegion>\n"
		"   <pwg:Height>%d</pwg:Height>\n"
		"   <pwg:Width>%d</pwg:Width>\n"
		"   <pwg:XOffset>0</pwg:XOffset>\n"
		"   <pwg:YOffset>0</pwg:YOffset>\n"
		"  </pwg:ScanRegion>\n"
		" </pwg:ScanRegions>\n"
		" <pwg:InputSource>%s</pwg:InputSource>\n"
		" <scan:ColorMode>%s</scan:ColorMode>\n"
		" <scan:XResolution>%d</scan:XResolution>\n"
		" <scan:YResolution>%d</scan:YResolution>\n"
		" <pwg:DocumentFormat>%s</pwg:DocumentFormat>\n"
		"</scan:ScanSettings>\n",
		opt.heightPx, opt.widthPx, opt.source.c_str(), opt.colorMode.c_str(),
		opt.resolution, opt.resolution, opt.format.c_str());
	return buf;
}

} // namespace xml

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

std::string HeaderValue(const std::string& headers, const std::string& name)
{
	// case-insensitive search for "name:"
	std::string lower = headers, lname = name + ":";
	for (char& c : lower) c = (char)tolower(c);
	for (char& c : lname) c = (char)tolower(c);
	size_t p = lower.find(lname);
	if (p == std::string::npos)
		return "";
	size_t vs = headers.find(':', p) + 1;
	while (vs < headers.size() && (headers[vs] == ' ' || headers[vs] == '\t')) ++vs;
	size_t ve = headers.find("\r\n", vs);
	return headers.substr(vs, ve == std::string::npos ? std::string::npos : ve - vs);
}
} // namespace

std::string EsclClient::BaseUrl() const
{
	char buf[512];
	std::snprintf(buf, sizeof(buf), "http://%s:%d/%s", fHost.c_str(), fPort, fRs.c_str());
	return buf;
}

EsclClient::Response EsclClient::HttpRequest(const std::string& method, const std::string& path,
	const std::string& contentType, const std::string& body)
{
	Response r;
	int fd = TcpConnect(fHost, fPort);
	if (fd < 0)
		return r;

	std::string req = method + " " + path + " HTTP/1.1\r\n";
	req += "Host: " + fHost + ":" + std::to_string(fPort) + "\r\n";
	if (!body.empty()) {
		req += "Content-Type: " + contentType + "\r\n";
		req += "Content-Length: " + std::to_string(body.size()) + "\r\n";
	}
	req += "Connection: close\r\n\r\n";
	req += body;

	if (write(fd, req.data(), req.size()) < 0) { close(fd); return r; }

	std::string resp;
	char buf[8192];
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
	r.location = HeaderValue(headers, "Location");
	r.body = resp.substr(hdrEnd + 4);
	return r;
}

std::vector<std::pair<std::string, std::string>> EsclClient::GetCapabilities(bool* okOut)
{
	*okOut = false;
	Response r = HttpRequest("GET", "/" + fRs + "/ScannerCapabilities", "", "");
	if (r.status != 200 || r.body.empty())
		return {};
	*okOut = true;
	std::vector<std::pair<std::string, std::string>> out;
	auto add = [&](const char* label, const std::string& tag) {
		std::string v = xml::Tag(r.body, tag);
		if (!v.empty()) out.push_back({label, v});
	};
	add("Produttore", "MakeAndModel");
	add("Numero di serie", "SerialNumber");
	add("Risoluzione max piano", "MaxWidth");
	// color modes and formats can repeat
	std::vector<std::string> modes = xml::Tags(r.body, "ColorMode");
	if (!modes.empty()) {
		std::string joined;
		for (size_t i = 0; i < modes.size(); ++i) joined += (i ? ", " : "") + modes[i];
		out.push_back({"Modi colore", joined});
	}
	std::vector<std::string> fmts = xml::Tags(r.body, "DocumentFormat");
	if (fmts.empty()) fmts = xml::Tags(r.body, "DocumentFormatExt");
	if (!fmts.empty()) {
		std::string joined;
		for (size_t i = 0; i < fmts.size(); ++i) joined += (i ? ", " : "") + fmts[i];
		out.push_back({"Formati", joined});
	}
	return out;
}

bool EsclClient::Scan(const std::string& destPath, const ScanOptions& opt)
{
	Response post = HttpRequest("POST", "/" + fRs + "/ScanJobs", "text/xml",
		xml::BuildScanSettings(opt));
	if (post.status != 201 || post.location.empty())
		return false;

	// The Location is an absolute or relative job URI; reduce it to a path.
	std::string jobPath = post.location;
	size_t scheme = jobPath.find("://");
	if (scheme != std::string::npos) {
		size_t slash = jobPath.find('/', scheme + 3);
		jobPath = (slash == std::string::npos) ? "/" : jobPath.substr(slash);
	}

	// Poll NextDocument: 200 => image bytes; 202 => still working; anything else => done/failed.
	for (int attempt = 0; attempt < 60; ++attempt) {
		Response doc = HttpRequest("GET", jobPath + "/NextDocument", "", "");
		if (doc.status == 200 && !doc.body.empty()) {
			FILE* f = std::fopen(destPath.c_str(), "wb");
			if (f == nullptr)
				return false;
			std::fwrite(doc.body.data(), 1, doc.body.size(), f);
			std::fclose(f);
			return true;
		}
		if (doc.status != 202)
			return false; // 404/410: no (more) documents, or an error
		// 202: keep polling (the caller runs this on a worker thread).
	}
	return false;
}

} // namespace escl
} // namespace campiello
