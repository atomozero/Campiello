// CastChannel.h
//
// A minimal CASTv2 client: the real Google Cast control channel, a TLS connection to port 8009 that
// carries length-prefixed `CastMessage` protobuf frames. Unlike DIAL (which only launches/stops a
// named receiver app), CASTv2 exposes the device's real state - volume, the running application, its
// media session - and can load a media URL into the Default Media Receiver with transport controls.
//
// Wire format (documented by the Cast SDK and reimplemented by pychromecast/node-castv2):
//   frame = uint32_be length + CastMessage protobuf
//   CastMessage fields: 1 protocol_version (varint), 2 source_id (string), 3 destination_id (string),
//                       4 namespace (string), 5 payload_type (varint, 0=STRING), 6 payload_utf8 (string)
// Control namespaces (JSON payloads):
//   urn:x-cast:com.google.cast.tp.connection  {"type":"CONNECT"} / {"type":"CLOSE"}
//   urn:x-cast:com.google.cast.tp.heartbeat    {"type":"PING"} / {"type":"PONG"}
//   urn:x-cast:com.google.cast.receiver        GET_STATUS / LAUNCH / STOP / SET_VOLUME
//   urn:x-cast:com.google.cast.media           LOAD / MEDIA GET_STATUS
//
// The protobuf codec is pure and unit-tested off-device (test_cast). The TLS I/O links OpenSSL
// (Apache-2.0) in the optional campiello_cast package; the device cert is self-signed so the client
// does not verify it (a LAN device, exactly like the Fire TV add-on). Screen mirroring is NOT here:
// it uses a separate proprietary Cast media-remoting channel and is a documented follow-up.
//
// References (behavioural, not code): pychromecast, node-castv2, the Google Cast protocol notes.

#ifndef CAMPIELLO_CAST_CASTCHANNEL_H
#define CAMPIELLO_CAST_CASTCHANNEL_H

#include <cstdint>
#include <string>

namespace campiello {
namespace cast {

// One decoded CastMessage (only the fields we use).
struct CastMessage {
	std::string source;
	std::string destination;
	std::string nameSpace;
	std::string payload; // payload_utf8
};

// Encode a STRING-payload CastMessage to wire bytes (no length prefix).
std::string EncodeCastMessage(const std::string& nameSpace, const std::string& source,
	const std::string& destination, const std::string& payload);

// Decode wire bytes (no length prefix) into a CastMessage. Returns false on a malformed buffer.
bool DecodeCastMessage(const std::string& bytes, CastMessage& out);

// The device state read over CASTv2.
struct CastStatus {
	bool ok = false;
	float volumeLevel = -1.0f; // 0..1, -1 if unknown
	bool muted = false;
	std::string appId;         // running receiver app id (e.g. CC1AD845)
	std::string displayName;   // "YouTube", "Default Media Receiver"...
	std::string statusText;    // free text the receiver shows
	std::string sessionId;     // application sessionId (for STOP)
	std::string transportId;   // destination for the media namespace
};

class CastChannel {
public:
	explicit CastChannel(const std::string& host, int port = 8009) : fHost(host), fPort(port) {}
	~CastChannel();
	CastChannel(const CastChannel&) = delete;
	CastChannel& operator=(const CastChannel&) = delete;

	// TLS-connect and open the platform virtual connection (CONNECT to receiver-0). false on failure.
	bool Connect();
	void Close();

	// Read the current receiver status (volume + running app). Requires Connect() first.
	CastStatus GetStatus();

	// Launch the Default Media Receiver (CC1AD845) and LOAD a media URL, opening a virtual connection
	// to the media session. contentType e.g. "video/mp4", "audio/mpeg". Returns true on a LOAD ack.
	bool CastUrl(const std::string& url, const std::string& contentType, const std::string& title);

	// Launch the Default Media Receiver once and open a virtual connection to its media session
	// (stores the transportId). Returns true when a media session is ready.
	bool LaunchMediaReceiver();

	// LOAD a media URL into the current media session (calls LaunchMediaReceiver first if needed).
	// waitStatus=true blocks for a MEDIA_STATUS/LOAD_FAILED reply; false fires and returns (used by
	// the ~1 fps screen preview, which LOADs a new image URL every frame and must not block).
	bool Load(const std::string& url, const std::string& contentType, const std::string& title,
		bool waitStatus);

	// Set the receiver volume (0..1). Returns true on ack.
	bool SetVolume(float level);

	// Stop the running application (needs its sessionId). Returns true on ack.
	bool StopApp(const std::string& sessionId);

	const char* Error() const { return fError; }

private:
	bool SendJson(const std::string& nameSpace, const std::string& destination,
		const std::string& payload);
	// Read frames until one arrives on `wantNamespace` whose payload contains `wantType` (a
	// "type":"..." match), or the deadline passes. Returns the payload, or "" on timeout.
	std::string ReadUntil(const std::string& wantNamespace, const std::string& wantType,
		int timeoutMs);
	bool WriteFrame(const std::string& bytes);
	bool ReadFrame(std::string& out, int timeoutMs);

	std::string fHost;
	int fPort;
	int fFd = -1;
	void* fSsl = nullptr;    // SSL*
	void* fCtx = nullptr;    // SSL_CTX*
	int fRequestId = 1;
	std::string fMediaTransportId; // media session destination, once LaunchMediaReceiver succeeds
	const char* fError = nullptr;
};

// Small JSON helpers (dependency-free): extract a string or number value by top-level-ish key.
std::string CastJsonString(const std::string& json, const std::string& key);
bool CastJsonNumber(const std::string& json, const std::string& key, double& out);

} // namespace cast
} // namespace campiello

#endif // CAMPIELLO_CAST_CASTCHANNEL_H
