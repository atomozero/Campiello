// CastChannel.cpp
//
// See CastChannel.h. Hand-rolled CASTv2: a CastMessage protobuf codec, tiny JSON readers, and a TLS
// channel over OpenSSL.

#include "CastChannel.h"

#include <chrono>
#include <cstdio>
#include <cstring>

#include <netdb.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <openssl/ssl.h>
#include <openssl/err.h>

namespace campiello {
namespace cast {

namespace {

const char* const kNsConnection = "urn:x-cast:com.google.cast.tp.connection";
const char* const kNsHeartbeat  = "urn:x-cast:com.google.cast.tp.heartbeat";
const char* const kNsReceiver   = "urn:x-cast:com.google.cast.receiver";
const char* const kNsMedia      = "urn:x-cast:com.google.cast.media";
const char* const kDefaultMediaReceiver = "CC1AD845";
const size_t kMaxFrame = 1 << 20; // 1 MiB guard

void PutVarint(std::string& out, uint64_t v)
{
	while (v >= 0x80) {
		out += static_cast<char>((v & 0x7f) | 0x80);
		v >>= 7;
	}
	out += static_cast<char>(v);
}

bool GetVarint(const uint8_t* p, size_t len, size_t& i, uint64_t& out)
{
	out = 0;
	int shift = 0;
	while (i < len) {
		uint8_t b = p[i++];
		out |= static_cast<uint64_t>(b & 0x7f) << shift;
		if ((b & 0x80) == 0)
			return true;
		shift += 7;
		if (shift > 63)
			return false;
	}
	return false;
}

void PutTag(std::string& out, int field, int wireType)
{
	PutVarint(out, (static_cast<uint64_t>(field) << 3) | wireType);
}

void PutString(std::string& out, int field, const std::string& s)
{
	PutTag(out, field, 2);
	PutVarint(out, s.size());
	out += s;
}

} // namespace

std::string EncodeCastMessage(const std::string& nameSpace, const std::string& source,
	const std::string& destination, const std::string& payload)
{
	std::string o;
	PutTag(o, 1, 0); PutVarint(o, 0);      // protocol_version = CASTV2_1_0
	PutString(o, 2, source);
	PutString(o, 3, destination);
	PutString(o, 4, nameSpace);
	PutTag(o, 5, 0); PutVarint(o, 0);      // payload_type = STRING
	PutString(o, 6, payload);
	return o;
}

bool DecodeCastMessage(const std::string& bytes, CastMessage& out)
{
	const uint8_t* p = reinterpret_cast<const uint8_t*>(bytes.data());
	size_t len = bytes.size();
	size_t i = 0;
	out = CastMessage();
	while (i < len) {
		uint64_t tag = 0;
		if (!GetVarint(p, len, i, tag))
			return false;
		int field = static_cast<int>(tag >> 3);
		int wt = static_cast<int>(tag & 7);
		if (wt == 0) {
			uint64_t v = 0;
			if (!GetVarint(p, len, i, v))
				return false;
		} else if (wt == 2) {
			uint64_t sz = 0;
			if (!GetVarint(p, len, i, sz) || i + sz > len)
				return false;
			std::string s(reinterpret_cast<const char*>(p + i), sz);
			i += sz;
			switch (field) {
				case 2: out.source = s; break;
				case 3: out.destination = s; break;
				case 4: out.nameSpace = s; break;
				case 6: out.payload = s; break;
				default: break;
			}
		} else if (wt == 5) {
			i += 4;
		} else if (wt == 1) {
			i += 8;
		} else {
			return false; // groups/unknown wire types not expected
		}
	}
	return true;
}

// --------------------------------------------------------------------------- JSON helpers
namespace {
size_t FindKey(const std::string& json, const std::string& key, size_t from)
{
	std::string needle = "\"" + key + "\"";
	size_t k = json.find(needle, from);
	if (k == std::string::npos)
		return std::string::npos;
	size_t colon = json.find(':', k + needle.size());
	return colon == std::string::npos ? std::string::npos : colon + 1;
}
} // namespace

std::string CastJsonString(const std::string& json, const std::string& key)
{
	size_t pos = FindKey(json, key, 0);
	if (pos == std::string::npos)
		return "";
	while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t'))
		++pos;
	if (pos >= json.size() || json[pos] != '"')
		return "";
	++pos;
	std::string out;
	while (pos < json.size()) {
		char c = json[pos++];
		if (c == '\\' && pos < json.size()) { out += json[pos++]; continue; }
		if (c == '"')
			break;
		out += c;
	}
	return out;
}

bool CastJsonNumber(const std::string& json, const std::string& key, double& out)
{
	size_t pos = FindKey(json, key, 0);
	if (pos == std::string::npos)
		return false;
	while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t'))
		++pos;
	size_t start = pos;
	while (pos < json.size() && (isdigit((unsigned char)json[pos]) || json[pos] == '-'
			|| json[pos] == '+' || json[pos] == '.' || json[pos] == 'e' || json[pos] == 'E'))
		++pos;
	if (pos == start)
		return false;
	out = std::strtod(json.substr(start, pos - start).c_str(), nullptr);
	return true;
}

// --------------------------------------------------------------------------- TLS channel
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

CastChannel::~CastChannel()
{
	Close();
}

bool CastChannel::Connect()
{
	fFd = TcpConnect(fHost, fPort);
	if (fFd < 0) { fError = "connessione TCP non riuscita"; return false; }

	SSL_CTX* ctx = SSL_CTX_new(TLS_client_method());
	if (ctx == nullptr) { fError = "SSL_CTX"; Close(); return false; }
	SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, nullptr); // device cert is self-signed (LAN)
	SSL* ssl = SSL_new(ctx);
	if (ssl == nullptr) { SSL_CTX_free(ctx); fError = "SSL_new"; Close(); return false; }
	SSL_set_fd(ssl, fFd);
	if (SSL_connect(ssl) != 1) {
		SSL_free(ssl); SSL_CTX_free(ctx);
		fSsl = nullptr; fCtx = nullptr;
		fError = "handshake TLS non riuscito";
		Close();
		return false;
	}
	fCtx = ctx;
	fSsl = ssl;

	// Open the platform virtual connection so the receiver accepts our requests.
	return SendJson(kNsConnection, "receiver-0", "{\"type\":\"CONNECT\"}");
}

void CastChannel::Close()
{
	if (fSsl != nullptr) {
		SSL_shutdown(static_cast<SSL*>(fSsl));
		SSL_free(static_cast<SSL*>(fSsl));
		fSsl = nullptr;
	}
	if (fCtx != nullptr) {
		SSL_CTX_free(static_cast<SSL_CTX*>(fCtx));
		fCtx = nullptr;
	}
	if (fFd >= 0) {
		close(fFd);
		fFd = -1;
	}
}

bool CastChannel::WriteFrame(const std::string& bytes)
{
	if (fSsl == nullptr)
		return false;
	uint32_t n = static_cast<uint32_t>(bytes.size());
	unsigned char hdr[4] = {
		static_cast<unsigned char>((n >> 24) & 0xff), static_cast<unsigned char>((n >> 16) & 0xff),
		static_cast<unsigned char>((n >> 8) & 0xff), static_cast<unsigned char>(n & 0xff)};
	std::string frame(reinterpret_cast<char*>(hdr), 4);
	frame += bytes;
	size_t off = 0;
	while (off < frame.size()) {
		int w = SSL_write(static_cast<SSL*>(fSsl), frame.data() + off, frame.size() - off);
		if (w <= 0)
			return false;
		off += w;
	}
	return true;
}

bool CastChannel::ReadFrame(std::string& out, int timeoutMs)
{
	if (fSsl == nullptr)
		return false;
	struct timeval tv;
	tv.tv_sec = timeoutMs / 1000;
	tv.tv_usec = (timeoutMs % 1000) * 1000;
	setsockopt(fFd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

	auto readExact = [&](char* buf, size_t need) -> bool {
		size_t got = 0;
		while (got < need) {
			int r = SSL_read(static_cast<SSL*>(fSsl), buf + got, need - got);
			if (r <= 0)
				return false;
			got += r;
		}
		return true;
	};

	unsigned char hdr[4];
	if (!readExact(reinterpret_cast<char*>(hdr), 4))
		return false;
	size_t len = (static_cast<size_t>(hdr[0]) << 24) | (static_cast<size_t>(hdr[1]) << 16)
		| (static_cast<size_t>(hdr[2]) << 8) | static_cast<size_t>(hdr[3]);
	if (len == 0 || len > kMaxFrame)
		return false;
	out.resize(len);
	return readExact(&out[0], len);
}

bool CastChannel::SendJson(const std::string& nameSpace, const std::string& destination,
	const std::string& payload)
{
	return WriteFrame(EncodeCastMessage(nameSpace, "sender-0", destination, payload));
}

std::string CastChannel::ReadUntil(const std::string& wantNamespace, const std::string& wantType,
	int timeoutMs)
{
	auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
	std::string typeNeedle = "\"type\":\"" + wantType + "\"";
	while (std::chrono::steady_clock::now() < deadline) {
		std::string frame;
		if (!ReadFrame(frame, 1500))
			continue;
		CastMessage m;
		if (!DecodeCastMessage(frame, m))
			continue;
		// Keep the connection alive: answer heartbeats.
		if (m.nameSpace == kNsHeartbeat && m.payload.find("PING") != std::string::npos) {
			SendJson(kNsHeartbeat, m.source.empty() ? "receiver-0" : m.source, "{\"type\":\"PONG\"}");
			continue;
		}
		if (m.nameSpace == wantNamespace
			&& (wantType.empty() || m.payload.find(typeNeedle) != std::string::npos))
			return m.payload;
	}
	return "";
}

static void ParseReceiverStatus(const std::string& json, CastStatus& st)
{
	st.appId = CastJsonString(json, "appId");
	st.displayName = CastJsonString(json, "displayName");
	st.statusText = CastJsonString(json, "statusText");
	st.sessionId = CastJsonString(json, "sessionId");
	st.transportId = CastJsonString(json, "transportId");
	double level = -1.0;
	if (CastJsonNumber(json, "level", level))
		st.volumeLevel = static_cast<float>(level);
	st.muted = json.find("\"muted\":true") != std::string::npos;
}

CastStatus CastChannel::GetStatus()
{
	CastStatus st;
	char req[96];
	std::snprintf(req, sizeof(req), "{\"type\":\"GET_STATUS\",\"requestId\":%d}", fRequestId++);
	if (!SendJson(kNsReceiver, "receiver-0", req))
		return st;
	std::string payload = ReadUntil(kNsReceiver, "RECEIVER_STATUS", 4000);
	if (payload.empty())
		return st;
	st.ok = true;
	ParseReceiverStatus(payload, st);
	return st;
}

bool CastChannel::SetVolume(float level)
{
	if (level < 0.0f) level = 0.0f;
	if (level > 1.0f) level = 1.0f;
	char req[128];
	std::snprintf(req, sizeof(req),
		"{\"type\":\"SET_VOLUME\",\"volume\":{\"level\":%.3f},\"requestId\":%d}", level,
		fRequestId++);
	if (!SendJson(kNsReceiver, "receiver-0", req))
		return false;
	return !ReadUntil(kNsReceiver, "RECEIVER_STATUS", 3000).empty();
}

bool CastChannel::StopApp(const std::string& sessionId)
{
	if (sessionId.empty())
		return false;
	char req[192];
	std::snprintf(req, sizeof(req),
		"{\"type\":\"STOP\",\"sessionId\":\"%s\",\"requestId\":%d}", sessionId.c_str(),
		fRequestId++);
	if (!SendJson(kNsReceiver, "receiver-0", req))
		return false;
	return !ReadUntil(kNsReceiver, "RECEIVER_STATUS", 3000).empty();
}

bool CastChannel::LaunchAppById(const std::string& appId, std::string& transportIdOut)
{
	// 1. LAUNCH the app.
	char launch[160];
	std::snprintf(launch, sizeof(launch),
		"{\"type\":\"LAUNCH\",\"appId\":\"%s\",\"requestId\":%d}", appId.c_str(), fRequestId++);
	if (!SendJson(kNsReceiver, "receiver-0", launch)) {
		fError = "LAUNCH non inviato";
		return false;
	}

	// 2. Wait for a RECEIVER_STATUS that carries this app's transportId.
	std::string transportId;
	auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(10000);
	while (std::chrono::steady_clock::now() < deadline) {
		std::string payload = ReadUntil(kNsReceiver, "RECEIVER_STATUS", 3000);
		if (payload.empty())
			continue;
		if (payload.find(appId) == std::string::npos)
			continue;
		transportId = CastJsonString(payload, "transportId");
		if (!transportId.empty())
			break;
	}
	if (transportId.empty()) {
		fError = "sessione app non avviata";
		return false;
	}

	// 3. Open a virtual connection to the app session.
	SendJson(kNsConnection, transportId, "{\"type\":\"CONNECT\"}");
	transportIdOut = transportId;
	return true;
}

bool CastChannel::OpenConnection(const std::string& destination)
{
	return SendJson(kNsConnection, destination, "{\"type\":\"CONNECT\"}");
}

bool CastChannel::Send(const std::string& nameSpace, const std::string& destination,
	const std::string& payload)
{
	return SendJson(nameSpace, destination, payload);
}

std::string CastChannel::Receive(const std::string& nameSpace, const std::string& wantType,
	int timeoutMs)
{
	return ReadUntil(nameSpace, wantType, timeoutMs);
}

bool CastChannel::LaunchMediaReceiver()
{
	if (!fMediaTransportId.empty())
		return true;
	return LaunchAppById(kDefaultMediaReceiver, fMediaTransportId);
}

bool CastChannel::Load(const std::string& url, const std::string& contentType,
	const std::string& title, bool waitStatus)
{
	if (fMediaTransportId.empty() && !LaunchMediaReceiver())
		return false;

	std::string ct = contentType.empty() ? "video/mp4" : contentType;
	// Images are shown as stills (streamType NONE); audio/video are buffered streams.
	std::string streamType = (ct.rfind("image/", 0) == 0) ? "NONE" : "BUFFERED";
	std::string load = "{\"type\":\"LOAD\",\"requestId\":" + std::to_string(fRequestId++)
		+ ",\"autoplay\":true,\"currentTime\":0,\"media\":{\"contentId\":\"" + url
		+ "\",\"streamType\":\"" + streamType + "\",\"contentType\":\"" + ct + "\"";
	if (!title.empty())
		load += ",\"metadata\":{\"metadataType\":0,\"title\":\"" + title + "\"}";
	load += "}}";
	if (!SendJson(kNsMedia, fMediaTransportId, load)) {
		fError = "LOAD non inviato";
		return false;
	}
	if (!waitStatus)
		return true;

	// Expect a MEDIA_STATUS (success) rather than a LOAD_FAILED.
	auto mediaDeadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(8000);
	while (std::chrono::steady_clock::now() < mediaDeadline) {
		std::string frame;
		if (!ReadFrame(frame, 2000))
			continue;
		CastMessage m;
		if (!DecodeCastMessage(frame, m))
			continue;
		if (m.nameSpace == kNsHeartbeat && m.payload.find("PING") != std::string::npos) {
			SendJson(kNsHeartbeat, "receiver-0", "{\"type\":\"PONG\"}");
			continue;
		}
		if (m.nameSpace != kNsMedia)
			continue;
		if (m.payload.find("MEDIA_STATUS") != std::string::npos)
			return true;
		if (m.payload.find("LOAD_FAILED") != std::string::npos
			|| m.payload.find("LOAD_CANCELLED") != std::string::npos
			|| m.payload.find("INVALID_REQUEST") != std::string::npos) {
			fError = "il ricevitore ha rifiutato il media";
			return false;
		}
	}
	fError = "nessuna conferma dal ricevitore";
	return false;
}

bool CastChannel::CastUrl(const std::string& url, const std::string& contentType,
	const std::string& title)
{
	return Load(url, contentType, title, true);
}

} // namespace cast
} // namespace campiello
