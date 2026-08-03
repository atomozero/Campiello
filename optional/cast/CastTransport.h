// CastTransport.h
//
// Milestone 3 of true Cast Streaming (screen mirroring): the send-side transport - the Cast RTP
// packet format, the AES-128-CTR frame encryption, a UDP sender, and a minimal RTCP sender report.
// This is the layer that carries the VP8 frames from milestone 2 to the device, over the udpPort the
// device gives in the milestone-1 ANSWER.
//
// Honest scope and testability:
//   - The AES-128-CTR crypto is exercised against a NIST SP800-38A known-answer vector and by
//     encrypt/decrypt round-trips, so the cipher itself is proven correct.
//   - The RTP packetizer + parser + reassembly are unit-tested (a frame splits into packets and
//     reassembles byte-for-byte; header fields round-trip).
//   - The UDP sender is tested over loopback.
//   - The exact Cast wire conventions (RTP header bit layout, the per-frame IV derivation, the RTCP
//     compound layout) are implemented from the openscreen / Chromium `media/cast` definitions to the
//     best available description. They have NOT been validated against a physical Cast receiver (none
//     on the dev LAN), so byte-level interop with a real device is unverified - this is stated, not
//     hidden. The full RTCP feedback loop (receiver ACK/NACK -> retransmission) belongs to the live
//     loop, milestone 4.
//
// Optional-only: links OpenSSL for AES; the MIT core never depends on it.

#ifndef CAMPIELLO_CAST_CASTTRANSPORT_H
#define CAMPIELLO_CAST_CASTTRANSPORT_H

#include <cstdint>
#include <string>
#include <vector>

namespace campiello {
namespace cast {

// ----------------------------------------------------------------- AES-128-CTR frame crypto

// Build the per-frame 16-byte nonce: the aes_iv_mask with the 32-bit frame_id XORed big-endian into
// its first four bytes (the Cast/`media/cast` GetAesNonce convention).
void CastNonce(uint32_t frameId, const uint8_t ivMask[16], uint8_t out[16]);

class FrameCrypto {
public:
	FrameCrypto(const uint8_t key[16], const uint8_t ivMask[16]);

	// AES-128-CTR is symmetric, so Encrypt and Decrypt are the same transform with the per-frame
	// nonce; both are provided for clarity.
	std::string Encrypt(uint32_t frameId, const std::string& plain) const;
	std::string Decrypt(uint32_t frameId, const std::string& cipher) const;

private:
	uint8_t fKey[16];
	uint8_t fIvMask[16];
};

// ----------------------------------------------------------------- Cast RTP packet

struct RtpHeader {
	uint8_t  payloadType = 0;
	uint16_t sequenceNumber = 0;
	uint32_t rtpTimestamp = 0;
	uint32_t ssrc = 0;
	bool     keyFrame = false;
	uint8_t  frameId = 0;
	uint16_t packetId = 0;
	uint16_t maxPacketId = 0;
	bool     hasReference = false;
	uint8_t  referenceFrameId = 0;
};

// Serialize one Cast RTP packet (12-byte RTP header + the Cast-specific header + payload).
std::string BuildRtpPacket(const RtpHeader& h, const std::string& payload);

// Parse a Cast RTP packet. Returns false if it is too short / malformed.
bool ParseRtpPacket(const std::string& packet, RtpHeader& h, std::string& payload);

// Split one (already-encrypted) frame into Cast RTP packets of at most `maxPacketBytes` on the wire.
// packetId runs 0..maxPacketId; every packet of a key frame carries the key bit.
std::vector<std::string> Packetize(uint32_t ssrc, uint8_t payloadType, uint16_t seqStart,
	uint8_t frameId, bool keyFrame, uint32_t rtpTimestamp, bool hasReference, uint8_t referenceFrameId,
	const std::string& encryptedFrame, size_t maxPacketBytes);

// Reassemble a frame from its packets (any order). Returns "" if packets are missing/inconsistent.
std::string Reassemble(const std::vector<std::string>& packets);

// ----------------------------------------------------------------- UDP sender

class UdpSender {
public:
	UdpSender() = default;
	~UdpSender();
	UdpSender(const UdpSender&) = delete;
	UdpSender& operator=(const UdpSender&) = delete;

	bool Open(const std::string& host, int port);
	bool Send(const std::string& datagram);
	void Close();
	bool IsOpen() const { return fFd >= 0; }

private:
	int fFd = -1;
};

// ----------------------------------------------------------------- minimal RTCP sender report

// Build an RTCP Sender Report (RFC 3550, PT=200, no report blocks): sender SSRC + NTP timestamp
// (msw/lsw) + RTP timestamp + packet/octet counts. Used for lip-sync/timing.
std::string BuildSenderReport(uint32_t ssrc, uint32_t ntpSeconds, uint32_t ntpFraction,
	uint32_t rtpTimestamp, uint32_t packetCount, uint32_t octetCount);

} // namespace cast
} // namespace campiello

#endif // CAMPIELLO_CAST_CASTTRANSPORT_H
