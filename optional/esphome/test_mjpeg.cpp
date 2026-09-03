// test_mjpeg.cpp
//
// Unit test for the MJPEG multipart parser (synthetic buffer, no network), plus an optional live
// integration grab: `test_mjpeg <host> [stream_port] [snapshot_port]` connects to a real ESP32-CAM,
// pulls a few frames and a snapshot and checks each is a valid JPEG (SOI FFD8 .. EOI FFD9).

#include "MjpegStream.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace campiello::esphome;

static int g_fail = 0;
#define CHECK(cond, msg) do { \
	if (!(cond)) { std::printf("FAIL: %s\n", msg); ++g_fail; } \
	else std::printf("ok  : %s\n", msg); \
} while (0)

static bool IsJpeg(const std::vector<unsigned char>& f)
{
	return f.size() > 4 && f[0] == 0xFF && f[1] == 0xD8
		&& f[f.size() - 2] == 0xFF && f[f.size() - 1] == 0xD9;
}

int main(int argc, char** argv)
{
	// ---- boundary parsing ----
	CHECK(MjpegStream::BoundaryFromContentType(
		"multipart/x-mixed-replace;boundary=123456789000000000000987654321")
		== "123456789000000000000987654321", "boundary parsed (no space)");
	CHECK(MjpegStream::BoundaryFromContentType(
		"multipart/x-mixed-replace; boundary=\"frame\"") == "frame", "boundary parsed (quoted)");
	CHECK(MjpegStream::BoundaryFromContentType("image/jpeg").empty(), "no boundary -> empty");

	// ---- synthetic multipart parse (Content-Length driven) ----
	{
		std::string jpegA = "\xFF\xD8" "AAAA" "\xFF\xD9";   // 8 bytes
		std::string jpegB = "\xFF\xD8" "BBBBBB" "\xFF\xD9"; // 10 bytes
		std::string body;
		body += "--BND\r\nContent-Type: image/jpeg\r\nContent-Length: 8\r\n\r\n" + jpegA + "\r\n";
		body += "--BND\r\nContent-Type: image/jpeg\r\nContent-Length: 10\r\n\r\n" + jpegB + "\r\n";
		body += "--BND--\r\n";

		MjpegStream s("test", 0);
		s.SetBoundaryForTest("BND");
		s.PreloadForTest(body);

		std::vector<unsigned char> f;
		std::string err;
		bool ok1 = s.NextFrame(f, &err);
		CHECK(ok1 && f.size() == 8 && IsJpeg(f), "frame 1 extracted (len 8, valid JPEG)");
		bool ok2 = s.NextFrame(f, &err);
		CHECK(ok2 && f.size() == 10 && IsJpeg(f), "frame 2 extracted (len 10, valid JPEG)");
		bool ok3 = s.NextFrame(f, &err);
		CHECK(!ok3, "no third frame (clean EOF)");
	}

	// ---- optional live integration ----
	if (argc >= 2) {
		std::string host = argv[1];
		int sport = (argc >= 3) ? std::atoi(argv[2]) : kDefaultStreamPort;
		int nport = (argc >= 4) ? std::atoi(argv[3]) : kDefaultSnapshotPort;
		std::printf("\n-- live grab from %s (stream %d, snapshot %d) --\n", host.c_str(), sport, nport);

		MjpegStream s(host, sport, "/");
		std::string err;
		if (s.Open(&err)) {
			int good = 0;
			for (int i = 0; i < 5; ++i) {
				std::vector<unsigned char> f;
				if (!s.NextFrame(f, &err)) { std::printf("  frame %d: %s\n", i, err.c_str()); break; }
				bool jp = IsJpeg(f);
				std::printf("  frame %d: %zu bytes, valid JPEG=%d\n", i, f.size(), (int)jp);
				if (jp) ++good;
			}
			s.Close();
			CHECK(good >= 3, "live: got >=3 valid JPEG frames from the stream");
		} else {
			std::printf("  Open failed: %s\n", err.c_str());
			CHECK(false, "live: stream opened");
		}

		std::vector<unsigned char> snap;
		if (MjpegStream::Snapshot(host, nport, "/", snap, &err))
			CHECK(IsJpeg(snap), "live: snapshot is a valid JPEG");
		else
			std::printf("  snapshot: %s\n", err.c_str());
	} else {
		std::printf("\n(pass <host> [stream_port] [snapshot_port] to run the live grab)\n");
	}

	std::printf(g_fail ? "\n%d FAILED\n" : "\nALL PASSED\n", g_fail);
	return g_fail ? 1 : 0;
}
