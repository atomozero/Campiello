// CastTransport.cpp
//
// See CastTransport.h. Cast RTP packetization, AES-128-CTR frame crypto, UDP sender, RTCP SR.

#include "CastTransport.h"

#include <algorithm>
#include <cstring>
#include <map>

#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>

#include <openssl/evp.h>

namespace campiello {
namespace cast {

// ----------------------------------------------------------------- crypto

void CastNonce(uint32_t frameId, const uint8_t ivMask[16], uint8_t out[16])
{
	std::memcpy(out, ivMask, 16);
	out[0] ^= static_cast<uint8_t>((frameId >> 24) & 0xff);
	out[1] ^= static_cast<uint8_t>((frameId >> 16) & 0xff);
	out[2] ^= static_cast<uint8_t>((frameId >> 8) & 0xff);
	out[3] ^= static_cast<uint8_t>(frameId & 0xff);
}

FrameCrypto::FrameCrypto(const uint8_t key[16], const uint8_t ivMask[16])
{
	std::memcpy(fKey, key, 16);
	std::memcpy(fIvMask, ivMask, 16);
}

namespace {
std::string AesCtr(const uint8_t key[16], const uint8_t iv[16], const std::string& in)
{
	EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
	if (ctx == nullptr)
		return "";
	std::string out;
	out.resize(in.size());
	int outl = 0;
	std::string result;
	if (EVP_EncryptInit_ex(ctx, EVP_aes_128_ctr(), nullptr, key, iv) == 1
		&& EVP_EncryptUpdate(ctx, reinterpret_cast<uint8_t*>(out.empty() ? nullptr : &out[0]),
			&outl, reinterpret_cast<const uint8_t*>(in.data()),
			static_cast<int>(in.size())) == 1) {
		int finl = 0;
		EVP_EncryptFinal_ex(ctx, reinterpret_cast<uint8_t*>(out.data()) + outl, &finl);
		out.resize(outl + finl);
		result = out;
	}
	EVP_CIPHER_CTX_free(ctx);
	return result;
}
} // namespace

std::string FrameCrypto::Encrypt(uint32_t frameId, const std::string& plain) const
{
	uint8_t iv[16];
	CastNonce(frameId, fIvMask, iv);
	return AesCtr(fKey, iv, plain);
}

std::string FrameCrypto::Decrypt(uint32_t frameId, const std::string& cipher) const
{
	return Encrypt(frameId, cipher); // CTR is symmetric
}

// ----------------------------------------------------------------- RTP

namespace {
void PutBE16(std::string& s, uint16_t v)
{
	s += static_cast<char>((v >> 8) & 0xff);
	s += static_cast<char>(v & 0xff);
}
void PutBE32(std::string& s, uint32_t v)
{
	s += static_cast<char>((v >> 24) & 0xff);
	s += static_cast<char>((v >> 16) & 0xff);
	s += static_cast<char>((v >> 8) & 0xff);
	s += static_cast<char>(v & 0xff);
}
uint16_t GetBE16(const uint8_t* p) { return (uint16_t(p[0]) << 8) | p[1]; }
uint32_t GetBE32(const uint8_t* p)
{
	return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}

// The Cast-specific header is 6 bytes, or 7 when a reference frame id is present.
size_t CastHeaderSize(bool hasReference) { return hasReference ? 7 : 6; }
} // namespace

std::string BuildRtpPacket(const RtpHeader& h, const std::string& payload)
{
	std::string p;
	p.reserve(12 + CastHeaderSize(h.hasReference) + payload.size());
	// Standard RTP fixed header (V=2, no padding/extension/CSRC, marker off).
	p += static_cast<char>(0x80);
	p += static_cast<char>(h.payloadType & 0x7f);
	PutBE16(p, h.sequenceNumber);
	PutBE32(p, h.rtpTimestamp);
	PutBE32(p, h.ssrc);
	// Cast header.
	uint8_t b12 = 0;
	if (h.keyFrame) b12 |= 0x80;
	if (h.hasReference) b12 |= 0x40;
	// low 6 bits: extension count = 0
	p += static_cast<char>(b12);
	p += static_cast<char>(h.frameId);
	PutBE16(p, h.packetId);
	PutBE16(p, h.maxPacketId);
	if (h.hasReference)
		p += static_cast<char>(h.referenceFrameId);
	p += payload;
	return p;
}

bool ParseRtpPacket(const std::string& packet, RtpHeader& h, std::string& payload)
{
	const uint8_t* p = reinterpret_cast<const uint8_t*>(packet.data());
	size_t n = packet.size();
	if (n < 18) // 12 RTP + at least 6 Cast bytes
		return false;
	if ((p[0] & 0xC0) != 0x80) // V must be 2
		return false;
	h.payloadType = p[1] & 0x7f;
	h.sequenceNumber = GetBE16(p + 2);
	h.rtpTimestamp = GetBE32(p + 4);
	h.ssrc = GetBE32(p + 8);
	h.keyFrame = (p[12] & 0x80) != 0;
	h.hasReference = (p[12] & 0x40) != 0;
	h.frameId = p[13];
	h.packetId = GetBE16(p + 14);
	h.maxPacketId = GetBE16(p + 16);
	size_t off = 18;
	if (h.hasReference) {
		if (n < 19)
			return false;
		h.referenceFrameId = p[18];
		off = 19;
	}
	payload.assign(packet.data() + off, packet.size() - off);
	return true;
}

std::vector<std::string> Packetize(uint32_t ssrc, uint8_t payloadType, uint16_t seqStart,
	uint8_t frameId, bool keyFrame, uint32_t rtpTimestamp, bool hasReference,
	uint8_t referenceFrameId, const std::string& encryptedFrame, size_t maxPacketBytes)
{
	std::vector<std::string> out;
	size_t headerBytes = 12 + CastHeaderSize(hasReference);
	if (maxPacketBytes <= headerBytes)
		return out; // no room for payload
	size_t maxPayload = maxPacketBytes - headerBytes;
	size_t total = encryptedFrame.size();
	size_t count = total == 0 ? 1 : (total + maxPayload - 1) / maxPayload;
	uint16_t maxPacketId = static_cast<uint16_t>(count - 1);

	for (size_t i = 0; i < count; ++i) {
		RtpHeader h;
		h.payloadType = payloadType;
		h.sequenceNumber = static_cast<uint16_t>(seqStart + i);
		h.rtpTimestamp = rtpTimestamp;
		h.ssrc = ssrc;
		h.keyFrame = keyFrame;
		h.frameId = frameId;
		h.packetId = static_cast<uint16_t>(i);
		h.maxPacketId = maxPacketId;
		h.hasReference = hasReference;
		h.referenceFrameId = referenceFrameId;
		size_t start = i * maxPayload;
		size_t len = std::min(maxPayload, total - start);
		out.push_back(BuildRtpPacket(h, encryptedFrame.substr(start, len)));
	}
	return out;
}

std::string Reassemble(const std::vector<std::string>& packets)
{
	std::map<uint16_t, std::string> byId;
	int expectedMax = -1;
	for (const std::string& pkt : packets) {
		RtpHeader h;
		std::string payload;
		if (!ParseRtpPacket(pkt, h, payload))
			return "";
		if (expectedMax < 0)
			expectedMax = h.maxPacketId;
		else if (expectedMax != h.maxPacketId)
			return ""; // inconsistent framing
		byId[h.packetId] = payload;
	}
	if (expectedMax < 0)
		return "";
	if (byId.size() != static_cast<size_t>(expectedMax) + 1)
		return ""; // missing packets
	std::string frame;
	for (int i = 0; i <= expectedMax; ++i) {
		auto it = byId.find(static_cast<uint16_t>(i));
		if (it == byId.end())
			return "";
		frame += it->second;
	}
	return frame;
}

// ----------------------------------------------------------------- UDP

UdpSender::~UdpSender()
{
	Close();
}

bool UdpSender::Open(const std::string& host, int port)
{
	Close();
	char portStr[8];
	std::snprintf(portStr, sizeof(portStr), "%d", port);
	struct addrinfo hints;
	std::memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_DGRAM;
	struct addrinfo* res = nullptr;
	if (getaddrinfo(host.c_str(), portStr, &hints, &res) != 0 || res == nullptr)
		return false;
	int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
	if (fd < 0) { freeaddrinfo(res); return false; }
	// connect() a UDP socket so subsequent send() go to this peer.
	if (connect(fd, res->ai_addr, res->ai_addrlen) != 0) {
		close(fd); freeaddrinfo(res); return false;
	}
	freeaddrinfo(res);
	fFd = fd;
	return true;
}

bool UdpSender::Send(const std::string& datagram)
{
	if (fFd < 0)
		return false;
	ssize_t n = send(fFd, datagram.data(), datagram.size(), 0);
	return n == static_cast<ssize_t>(datagram.size());
}

void UdpSender::Close()
{
	if (fFd >= 0) {
		close(fFd);
		fFd = -1;
	}
}

// ----------------------------------------------------------------- RTCP SR

std::string BuildSenderReport(uint32_t ssrc, uint32_t ntpSeconds, uint32_t ntpFraction,
	uint32_t rtpTimestamp, uint32_t packetCount, uint32_t octetCount)
{
	std::string p;
	// word 0: V=2, P=0, RC=0, PT=200 (SR), length = 6 (7 words total minus one)
	p += static_cast<char>(0x80);
	p += static_cast<char>(200);
	PutBE16(p, 6);
	PutBE32(p, ssrc);
	PutBE32(p, ntpSeconds);
	PutBE32(p, ntpFraction);
	PutBE32(p, rtpTimestamp);
	PutBE32(p, packetCount);
	PutBE32(p, octetCount);
	return p;
}

} // namespace cast
} // namespace campiello
