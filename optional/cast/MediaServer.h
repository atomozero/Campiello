// MediaServer.h
//
// A tiny single-file HTTP server used to cast a LOCAL media file to a Google Cast device. A
// Chromecast never receives a pushed stream: you give it a URL and it pulls the media over HTTP by
// itself. So to play a file that lives on this PC, Campiello serves that one file over HTTP (with the
// byte-range support the Chromecast needs to seek) and hands the device the URL
// http://<this-pc>:<port>/<name>; the Chromecast then downloads and plays it straight from us.
//
// Notes and honest limits:
//   - No transcoding. The file is served as-is, so the Chromecast must natively support the codec
//     (H.264/VP8/VP9 video, AAC/MP3/Opus/Vorbis audio in MP4/WebM/MKV). Unsupported media will be
//     rejected by the receiver, not converted.
//   - The server binds an ephemeral port on all interfaces and serves ONLY the one configured file,
//     for any request path, for as long as it runs (the device streams for the whole playback).
//   - Handles GET and HEAD, and Range requests (206 Partial Content with Content-Range).
//
// Portable: standard C++ + POSIX sockets, no BeAPI and no third-party dependency.

#ifndef CAMPIELLO_CAST_MEDIASERVER_H
#define CAMPIELLO_CAST_MEDIASERVER_H

#include <atomic>
#include <mutex>
#include <string>
#include <thread>

namespace campiello {
namespace cast {

class MediaServer {
public:
	MediaServer() = default;
	~MediaServer();
	MediaServer(const MediaServer&) = delete;
	MediaServer& operator=(const MediaServer&) = delete;

	// Start (or restart) serving `filePath` with the given Content-Type. Binds an ephemeral port on
	// 0.0.0.0 and runs an accept loop on a worker thread. Returns the bound port, or 0 on failure.
	int Serve(const std::string& filePath, const std::string& contentType);

	// Start (or restart) serving an in-memory buffer (initially empty) with the given Content-Type.
	// Used by the ~1 fps screen preview: each frame calls UpdateBuffer with a fresh JPEG. Returns the
	// bound port, or 0 on failure.
	int ServeBuffer(const std::string& contentType);

	// Atomically replace the buffer served in buffer mode (thread-safe). No-op in file mode.
	void UpdateBuffer(const std::string& bytes);

	// Stop the accept loop and close the listener. Idempotent; also called by the destructor.
	void Stop();

	bool Running() const { return fListen >= 0; }

	// The port currently bound (0 if not running).
	int Port() const { return fPort; }

private:
	void AcceptLoop();
	void HandleClient(int fd);

	int StartListener();

	int fListen = -1;
	int fPort = 0;
	std::thread fThread;
	std::atomic<bool> fStop{false};
	std::string fPath;         // file mode: the file to serve
	std::string fContentType;
	bool fBufferMode = false;  // true: serve fBuffer; false: serve fPath
	std::mutex fBufMutex;
	std::string fBuffer;       // buffer mode: current bytes (guarded by fBufMutex)
};

// Guess a Content-Type from a file name's extension (best-effort, Cast-relevant types).
std::string GuessMediaType(const std::string& name);

// Find this machine's local IPv4 address on the route toward `host` (a dotted IP or hostname), using
// a connect()ed UDP socket and getsockname. Returns "" on failure.
std::string LocalIpToward(const std::string& host);

} // namespace cast
} // namespace campiello

#endif // CAMPIELLO_CAST_MEDIASERVER_H
