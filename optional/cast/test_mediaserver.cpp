// test_mediaserver.cpp
//
// Integration test for MediaServer: starts it on a temp file and checks full GET, a Range request
// (206 + Content-Range + the right bytes) and HEAD (headers, no body). Build:
//   g++ -std=c++17 test_mediaserver.cpp MediaServer.cpp -lnetwork -o test_mediaserver && ./test_mediaserver

#include <cstdio>
#include <cstring>
#include <string>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include "MediaServer.h"

using namespace campiello::cast;

static int gFail = 0;
#define CHECK(cond) do { if (!(cond)) { \
	std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++gFail; } } while (0)

// Do one HTTP request to 127.0.0.1:port and return the raw response.
static std::string Request(int port, const std::string& req)
{
	int fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0) return "";
	sockaddr_in a; std::memset(&a, 0, sizeof(a));
	a.sin_family = AF_INET; a.sin_port = htons(port);
	inet_pton(AF_INET, "127.0.0.1", &a.sin_addr);
	if (connect(fd, (sockaddr*)&a, sizeof(a)) != 0) { close(fd); return ""; }
	send(fd, req.data(), req.size(), 0);
	std::string resp; char buf[4096]; ssize_t n;
	while ((n = recv(fd, buf, sizeof(buf), 0)) > 0) resp.append(buf, n);
	close(fd);
	return resp;
}

static std::string Body(const std::string& resp)
{
	size_t p = resp.find("\r\n\r\n");
	return p == std::string::npos ? "" : resp.substr(p + 4);
}

int main()
{
	// Build a temp file with 1000 known bytes ('A'+i%26).
	const char* path = "/tmp/campiello_mediatest.bin";
	{
		FILE* f = std::fopen(path, "wb");
		CHECK(f != nullptr);
		for (int i = 0; i < 1000; ++i) { char c = 'A' + (i % 26); std::fwrite(&c, 1, 1, f); }
		std::fclose(f);
	}

	MediaServer srv;
	int port = srv.Serve(path, "video/mp4");
	CHECK(port > 0);

	// Full GET -> 200, full body, Accept-Ranges advertised.
	{
		std::string r = Request(port, "GET /stream.mp4 HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n");
		CHECK(r.find("200 OK") != std::string::npos);
		CHECK(r.find("Content-Type: video/mp4") != std::string::npos);
		CHECK(r.find("Accept-Ranges: bytes") != std::string::npos);
		CHECK(r.find("Content-Length: 1000") != std::string::npos);
		CHECK(Body(r).size() == 1000);
	}

	// Range 10-19 -> 206, correct Content-Range, 10 bytes, right content.
	{
		std::string r = Request(port,
			"GET /stream.mp4 HTTP/1.1\r\nHost: x\r\nRange: bytes=10-19\r\nConnection: close\r\n\r\n");
		CHECK(r.find("206 Partial Content") != std::string::npos);
		CHECK(r.find("Content-Range: bytes 10-19/1000") != std::string::npos);
		CHECK(r.find("Content-Length: 10") != std::string::npos);
		std::string b = Body(r);
		CHECK(b.size() == 10);
		// byte i has value 'A'+(i%26); bytes 10..19 -> 'K'..'T'
		CHECK(b == "KLMNOPQRST");
	}

	// Open-ended range 995- -> last 5 bytes.
	{
		std::string r = Request(port,
			"GET /s HTTP/1.1\r\nHost: x\r\nRange: bytes=995-\r\nConnection: close\r\n\r\n");
		CHECK(r.find("206") != std::string::npos);
		CHECK(r.find("Content-Range: bytes 995-999/1000") != std::string::npos);
		CHECK(Body(r).size() == 5);
	}

	// HEAD -> headers, no body.
	{
		std::string r = Request(port, "HEAD /s HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n");
		CHECK(r.find("200 OK") != std::string::npos);
		CHECK(r.find("Content-Length: 1000") != std::string::npos);
		CHECK(Body(r).empty());
	}

	srv.Stop();
	CHECK(!srv.Running());

	// GuessMediaType.
	CHECK(GuessMediaType("movie.MP4") == "video/mp4");
	CHECK(GuessMediaType("song.mp3") == "audio/mpeg");
	CHECK(GuessMediaType("clip.webm") == "video/webm");
	CHECK(GuessMediaType("x.unknown") == "application/octet-stream");

	// LocalIpToward returns some dotted IPv4 for a routable target.
	{
		std::string ip = LocalIpToward("8.8.8.8");
		CHECK(!ip.empty());
		CHECK(ip.find('.') != std::string::npos);
	}

	std::remove(path);
	if (gFail == 0)
		std::printf("all MediaServer tests passed\n");
	return gFail == 0 ? 0 : 1;
}
