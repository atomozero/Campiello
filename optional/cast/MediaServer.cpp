// MediaServer.cpp
//
// See MediaServer.h. A minimal single-file HTTP/1.1 server with Range support.

#include "MediaServer.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

namespace campiello {
namespace cast {

std::string GuessMediaType(const std::string& name)
{
	std::string lower = name;
	std::transform(lower.begin(), lower.end(), lower.begin(),
		[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	auto ends = [&](const char* ext) {
		size_t n = std::strlen(ext);
		return lower.size() >= n && lower.compare(lower.size() - n, n, ext) == 0;
	};
	if (ends(".mp4") || ends(".m4v")) return "video/mp4";
	if (ends(".webm")) return "video/webm";
	if (ends(".mkv")) return "video/x-matroska";
	if (ends(".mov")) return "video/quicktime";
	if (ends(".m3u8")) return "application/x-mpegurl";
	if (ends(".mpd")) return "application/dash+xml";
	if (ends(".mp3")) return "audio/mpeg";
	if (ends(".m4a") || ends(".aac")) return "audio/aac";
	if (ends(".ogg") || ends(".opus")) return "audio/ogg";
	if (ends(".flac")) return "audio/flac";
	if (ends(".wav")) return "audio/wav";
	if (ends(".jpg") || ends(".jpeg")) return "image/jpeg";
	if (ends(".png")) return "image/png";
	return "application/octet-stream";
}

std::string LocalIpToward(const std::string& host)
{
	struct addrinfo hints;
	std::memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_DGRAM;
	struct addrinfo* res = nullptr;
	if (getaddrinfo(host.c_str(), "9", &hints, &res) != 0 || res == nullptr)
		return "";
	int s = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
	if (s < 0) { freeaddrinfo(res); return ""; }
	std::string out;
	if (connect(s, res->ai_addr, res->ai_addrlen) == 0) {
		struct sockaddr_in local;
		socklen_t len = sizeof(local);
		if (getsockname(s, reinterpret_cast<struct sockaddr*>(&local), &len) == 0) {
			char buf[INET_ADDRSTRLEN];
			if (inet_ntop(AF_INET, &local.sin_addr, buf, sizeof(buf)) != nullptr)
				out = buf;
		}
	}
	close(s);
	freeaddrinfo(res);
	return out;
}

MediaServer::~MediaServer()
{
	Stop();
}

int MediaServer::Serve(const std::string& filePath, const std::string& contentType)
{
	Stop();

	// The file must exist and be a regular file.
	struct stat st;
	if (stat(filePath.c_str(), &st) != 0 || !S_ISREG(st.st_mode))
		return 0;

	int fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0)
		return 0;
	int on = 1;
	setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

	struct sockaddr_in addr;
	std::memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	addr.sin_port = 0; // ephemeral
	if (bind(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) != 0
		|| listen(fd, 8) != 0) {
		close(fd);
		return 0;
	}
	socklen_t len = sizeof(addr);
	if (getsockname(fd, reinterpret_cast<struct sockaddr*>(&addr), &len) != 0) {
		close(fd);
		return 0;
	}

	fListen = fd;
	fPort = ntohs(addr.sin_port);
	fPath = filePath;
	fContentType = contentType.empty() ? GuessMediaType(filePath) : contentType;
	fStop.store(false);
	fThread = std::thread(&MediaServer::AcceptLoop, this);
	return fPort;
}

void MediaServer::Stop()
{
	if (fListen < 0)
		return;
	fStop.store(true);
	int fd = fListen;
	fListen = -1;
	// Shutdown then close to unblock accept().
	shutdown(fd, SHUT_RDWR);
	close(fd);
	if (fThread.joinable())
		fThread.join();
	fPort = 0;
}

void MediaServer::AcceptLoop()
{
	while (!fStop.load()) {
		int client = accept(fListen, nullptr, nullptr);
		if (client < 0) {
			if (fStop.load())
				break;
			continue;
		}
		HandleClient(client);
		close(client);
	}
}

namespace {

// Read the request head (up to the blank line). Returns false on connection close/error.
bool ReadHead(int fd, std::string& head)
{
	char buf[1024];
	while (head.find("\r\n\r\n") == std::string::npos && head.size() < 16384) {
		ssize_t n = recv(fd, buf, sizeof(buf), 0);
		if (n <= 0)
			return false;
		head.append(buf, n);
	}
	return head.find("\r\n\r\n") != std::string::npos;
}

// Parse a "Range: bytes=start-end" header (case-insensitive). Returns true if a range was found.
bool ParseRange(const std::string& head, off_t size, off_t& start, off_t& end)
{
	std::string lower = head;
	std::transform(lower.begin(), lower.end(), lower.begin(),
		[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	size_t p = lower.find("range:");
	if (p == std::string::npos)
		return false;
	p = lower.find("bytes=", p);
	if (p == std::string::npos)
		return false;
	p += 6;
	size_t dash = lower.find('-', p);
	if (dash == std::string::npos)
		return false;
	std::string s = lower.substr(p, dash - p);
	size_t crlf = lower.find_first_of("\r\n", dash);
	std::string e = lower.substr(dash + 1, crlf == std::string::npos ? std::string::npos : crlf - dash - 1);

	if (s.empty()) {
		// Suffix range "-N": the last N bytes.
		off_t n = std::strtoll(e.c_str(), nullptr, 10);
		if (n <= 0) return false;
		start = (n >= size) ? 0 : size - n;
		end = size - 1;
	} else {
		start = std::strtoll(s.c_str(), nullptr, 10);
		end = e.empty() ? size - 1 : std::strtoll(e.c_str(), nullptr, 10);
	}
	if (start < 0) start = 0;
	if (end >= size) end = size - 1;
	return start <= end;
}

bool SendAll(int fd, const char* data, size_t len)
{
	size_t off = 0;
	while (off < len) {
		ssize_t w = send(fd, data + off, len - off, 0);
		if (w <= 0)
			return false;
		off += w;
	}
	return true;
}

} // namespace

void MediaServer::HandleClient(int fd)
{
	std::string head;
	if (!ReadHead(fd, head))
		return;
	bool isHead = head.compare(0, 5, "HEAD ") == 0;

	FILE* f = std::fopen(fPath.c_str(), "rb");
	if (f == nullptr) {
		const char* nf = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
		SendAll(fd, nf, std::strlen(nf));
		return;
	}
	std::fseek(f, 0, SEEK_END);
	off_t size = std::ftell(f);

	off_t start = 0, end = size - 1;
	bool partial = ParseRange(head, size, start, end);
	off_t length = end - start + 1;

	char header[512];
	if (partial) {
		std::snprintf(header, sizeof(header),
			"HTTP/1.1 206 Partial Content\r\n"
			"Content-Type: %s\r\n"
			"Accept-Ranges: bytes\r\n"
			"Content-Range: bytes %lld-%lld/%lld\r\n"
			"Content-Length: %lld\r\n"
			"Connection: close\r\n\r\n",
			fContentType.c_str(), (long long)start, (long long)end, (long long)size,
			(long long)length);
	} else {
		std::snprintf(header, sizeof(header),
			"HTTP/1.1 200 OK\r\n"
			"Content-Type: %s\r\n"
			"Accept-Ranges: bytes\r\n"
			"Content-Length: %lld\r\n"
			"Connection: close\r\n\r\n",
			fContentType.c_str(), (long long)size);
	}
	if (!SendAll(fd, header, std::strlen(header)) || isHead) {
		std::fclose(f);
		return;
	}

	std::fseek(f, start, SEEK_SET);
	off_t remaining = length;
	char buf[65536];
	while (remaining > 0) {
		size_t want = remaining < (off_t)sizeof(buf) ? (size_t)remaining : sizeof(buf);
		size_t got = std::fread(buf, 1, want, f);
		if (got == 0)
			break;
		if (!SendAll(fd, buf, got))
			break; // client closed (Chromecast opens/closes ranges freely)
		remaining -= got;
	}
	std::fclose(f);
}

} // namespace cast
} // namespace campiello
