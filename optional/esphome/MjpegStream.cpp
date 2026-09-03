// MjpegStream.cpp
//
// See MjpegStream.h. Hand-rolled multipart/x-mixed-replace reader over a plain socket.

#include "MjpegStream.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>

namespace campiello {
namespace esphome {

const int kDefaultStreamPort   = 8080;
const int kDefaultSnapshotPort = 8081;

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

// Case-insensitive find.
size_t IFind(const std::string& hay, const std::string& needle, size_t from = 0)
{
	if (needle.empty() || from > hay.size())
		return std::string::npos;
	for (size_t i = from; i + needle.size() <= hay.size(); ++i) {
		size_t j = 0;
		for (; j < needle.size(); ++j)
			if (std::tolower((unsigned char)hay[i + j]) != std::tolower((unsigned char)needle[j]))
				break;
		if (j == needle.size())
			return i;
	}
	return std::string::npos;
}

// Parse the Content-Length value that appears in `headers` (a header block). -1 if absent.
long ParseContentLength(const std::string& headers)
{
	size_t k = IFind(headers, "content-length:");
	if (k == std::string::npos)
		return -1;
	size_t i = k + strlen("content-length:");
	while (i < headers.size() && (headers[i] == ' ' || headers[i] == '\t'))
		++i;
	long v = 0;
	bool any = false;
	while (i < headers.size() && std::isdigit((unsigned char)headers[i])) {
		v = v * 10 + (headers[i] - '0');
		++i;
		any = true;
	}
	return any ? v : -1;
}

} // namespace

std::string MjpegStream::BoundaryFromContentType(const std::string& contentType)
{
	size_t k = IFind(contentType, "boundary=");
	if (k == std::string::npos)
		return "";
	size_t start = k + strlen("boundary=");
	std::string b;
	for (size_t i = start; i < contentType.size(); ++i) {
		char c = contentType[i];
		if (c == '\r' || c == '\n' || c == ';' || c == ' ')
			break;
		b += c;
	}
	// Boundaries may be quoted.
	if (b.size() >= 2 && b.front() == '"' && b.back() == '"')
		b = b.substr(1, b.size() - 2);
	return b;
}

ssize_t MjpegStream::Fill()
{
	if (fFd < 0)
		return 0;   // closed / test-preloaded: no more data
	char tmp[8192];
	ssize_t n = read(fFd, tmp, sizeof(tmp));
	if (n > 0)
		fBuf.append(tmp, n);
	return n;
}

size_t MjpegStream::FindInBuffer(const std::string& marker, size_t from)
{
	return fBuf.find(marker, from);
}

bool MjpegStream::Open(std::string* err)
{
	auto fail = [&](const char* m) { if (err) *err = m; Close(); return false; };
	fFd = TcpConnect(fHost, fPort);
	if (fFd < 0)
		return fail("Connessione alla telecamera non riuscita.");

	std::string req = "GET " + fPath + " HTTP/1.0\r\n";
	req += "Host: " + fHost + ":" + std::to_string(fPort) + "\r\n";
	req += "User-Agent: Campiello/1.0\r\n";
	req += "Accept: multipart/x-mixed-replace, image/jpeg\r\n";
	req += "Connection: close\r\n\r\n";
	if (write(fFd, req.data(), req.size()) < 0)
		return fail("Invio della richiesta non riuscito.");

	// Read until the end of the HTTP response headers.
	size_t hdrEnd;
	while ((hdrEnd = FindInBuffer("\r\n\r\n", 0)) == std::string::npos) {
		if (Fill() <= 0)
			return fail("Risposta HTTP incompleta dalla telecamera.");
		if (fBuf.size() > 65536)
			return fail("Intestazioni HTTP troppo grandi.");
	}
	std::string headers = fBuf.substr(0, hdrEnd);
	// Status line must be 2xx.
	size_t sp = headers.find(' ');
	int status = (sp != std::string::npos) ? std::atoi(headers.c_str() + sp + 1) : 0;
	if (status < 200 || status >= 300)
		return fail("La telecamera ha risposto con un errore HTTP.");

	size_t ctk = IFind(headers, "content-type:");
	std::string ct = (ctk != std::string::npos)
		? headers.substr(ctk, headers.find("\r\n", ctk) - ctk) : "";
	if (IFind(ct, "multipart/") == std::string::npos)
		return fail("Il flusso non e' un MJPEG (multipart) valido.");
	fBoundary = BoundaryFromContentType(ct);
	if (fBoundary.empty())
		return fail("Boundary MJPEG mancante.");

	// Consume the header block; keep any body bytes already buffered.
	fBuf.erase(0, hdrEnd + 4);
	return true;
}

bool MjpegStream::NextFrame(std::vector<unsigned char>& out, std::string* err)
{
	if (fFd < 0 && fBuf.empty()) { if (err) *err = "Flusso non aperto."; return false; }
	const std::string dashBoundary = "--" + fBoundary;

	// 1) advance to the next boundary line.
	size_t b;
	while ((b = FindInBuffer(dashBoundary, 0)) == std::string::npos) {
		if (Fill() <= 0) { if (err) *err = "Fine del flusso."; return false; }
		if (fBuf.size() > 8 * 1024 * 1024) { if (err) *err = "Frame troppo grande."; return false; }
	}
	fBuf.erase(0, b + dashBoundary.size());

	// 2) read this part's headers (up to a blank line).
	size_t hdrEnd;
	while ((hdrEnd = FindInBuffer("\r\n\r\n", 0)) == std::string::npos) {
		if (Fill() <= 0) { if (err) *err = "Intestazioni del frame incomplete."; return false; }
		if (fBuf.size() > 65536) { if (err) *err = "Intestazioni del frame troppo grandi."; return false; }
	}
	std::string partHeaders = fBuf.substr(0, hdrEnd);
	long len = ParseContentLength(partHeaders);
	fBuf.erase(0, hdrEnd + 4);

	if (len > 0) {
		// 3a) Content-Length known: read exactly that many bytes.
		while (fBuf.size() < (size_t)len) {
			if (Fill() <= 0) { if (err) *err = "Frame troncato."; return false; }
		}
		out.assign(fBuf.begin(), fBuf.begin() + len);
		fBuf.erase(0, len);
		return true;
	}

	// 3b) No Content-Length: read until the next boundary.
	size_t next;
	while ((next = FindInBuffer(dashBoundary, 0)) == std::string::npos) {
		if (Fill() <= 0) { if (err) *err = "Fine del flusso."; return false; }
		if (fBuf.size() > 8 * 1024 * 1024) { if (err) *err = "Frame troppo grande."; return false; }
	}
	std::string frame = fBuf.substr(0, next);
	// trim a trailing CRLF before the boundary
	while (!frame.empty() && (frame.back() == '\r' || frame.back() == '\n'))
		frame.pop_back();
	out.assign(frame.begin(), frame.end());
	fBuf.erase(0, next);
	return true;
}

void MjpegStream::Close()
{
	if (fFd >= 0) {
		close(fFd);
		fFd = -1;
	}
	fBuf.clear();
}

bool MjpegStream::Snapshot(const std::string& host, int port, const std::string& path,
	std::vector<unsigned char>& out, std::string* err)
{
	auto fail = [&](const char* m) { if (err) *err = m; return false; };
	int fd = TcpConnect(host, port);
	if (fd < 0)
		return fail("Connessione alla telecamera non riuscita.");
	std::string p = path.empty() ? "/" : path;
	std::string req = "GET " + p + " HTTP/1.0\r\n";
	req += "Host: " + host + ":" + std::to_string(port) + "\r\n";
	req += "User-Agent: Campiello/1.0\r\nConnection: close\r\n\r\n";
	if (write(fd, req.data(), req.size()) < 0) { close(fd); return fail("Invio non riuscito."); }

	std::string resp;
	char tmp[8192];
	ssize_t n;
	while ((n = read(fd, tmp, sizeof(tmp))) > 0)
		resp.append(tmp, n);
	close(fd);

	size_t hdrEnd = resp.find("\r\n\r\n");
	if (hdrEnd == std::string::npos)
		return fail("Risposta HTTP incompleta.");
	size_t sp = resp.find(' ');
	int status = (sp != std::string::npos) ? std::atoi(resp.c_str() + sp + 1) : 0;
	if (status < 200 || status >= 300)
		return fail("La telecamera ha risposto con un errore HTTP.");
	std::string body = resp.substr(hdrEnd + 4);
	out.assign(body.begin(), body.end());
	return !out.empty();
}

} // namespace esphome
} // namespace campiello
