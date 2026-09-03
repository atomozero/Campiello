// MjpegStream.h
//
// A tiny dependency-light client for the Motion-JPEG stream an ESP32-CAM running ESPHome's
// esp32_camera_web_server exposes: an HTTP response of Content-Type multipart/x-mixed-replace whose
// parts are individual JPEG frames (each with its own Content-Length). Also a one-shot snapshot GET.
//
// Plain sockets + std only (no libbe), so it can be unit/integration-tested headless. The caller
// decodes the raw JPEG frames (e.g. with the Haiku Translation Kit) and displays them.
//
// ESPHome typically serves the stream on one port (commonly 8080) and a still snapshot on another
// (commonly 8081); both default here but are configurable. The native ESPHome API (protobuf on 6053,
// optionally Noise-encrypted) is NOT needed for video, which keeps this MIT-clean and simple.

#ifndef CAMPIELLO_ESPHOME_MJPEGSTREAM_H
#define CAMPIELLO_ESPHOME_MJPEGSTREAM_H

#include <string>
#include <vector>

namespace campiello {
namespace esphome {

// Default esp32_camera_web_server ports.
extern const int kDefaultStreamPort;    // 8080
extern const int kDefaultSnapshotPort;  // 8081

class MjpegStream {
public:
	MjpegStream(const std::string& host, int port, const std::string& path = "/")
		: fHost(host), fPort(port), fPath(path.empty() ? "/" : path) {}
	~MjpegStream() { Close(); }

	// Connect and read the HTTP headers; verifies the multipart content type and captures the
	// boundary. Returns false (with *err) on failure.
	bool Open(std::string* err);

	// Read the next JPEG frame into `out`. Returns false at EOF / on error.
	bool NextFrame(std::vector<unsigned char>& out, std::string* err);

	void Close();

	bool IsOpen() const { return fFd >= 0; }

	// One-shot: GET a single JPEG (the snapshot endpoint). Static, opens and closes its own socket.
	static bool Snapshot(const std::string& host, int port, const std::string& path,
		std::vector<unsigned char>& out, std::string* err);

	// Split a "multipart/...; boundary=XYZ" content-type value into the boundary token. Exposed for
	// tests. Returns "" if absent.
	static std::string BoundaryFromContentType(const std::string& contentType);

	// Test seams: preload a raw multipart body and set the boundary, then call NextFrame without a
	// socket (Fill() returns EOF once the preloaded bytes are consumed).
	void PreloadForTest(const std::string& body) { fBuf += body; }
	void SetBoundaryForTest(const std::string& boundary) { fBoundary = boundary; }

private:
	// Fill the internal buffer with more socket data. Returns bytes appended (0 = EOF/err).
	ssize_t Fill();
	// Read until `marker` is found in the buffer; returns the index of the marker start, or npos.
	size_t FindInBuffer(const std::string& marker, size_t from);

	std::string fHost;
	int         fPort;
	std::string fPath;
	std::string fBoundary;
	int         fFd = -1;
	std::string fBuf;   // unconsumed bytes read from the socket
};

} // namespace esphome
} // namespace campiello

#endif // CAMPIELLO_ESPHOME_MJPEGSTREAM_H
