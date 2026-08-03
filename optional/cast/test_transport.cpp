// test_transport.cpp
//
// Unit tests for the Cast Streaming transport (milestone 3): AES-128-CTR frame crypto (incl. a NIST
// known-answer vector), the Cast RTP packetizer/parser/reassembly, the UDP sender over loopback, and
// the RTCP sender-report layout. Build:
//   g++ -std=c++17 test_transport.cpp CastTransport.cpp -lssl -lcrypto -lnetwork -o test_transport
//   ./test_transport

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include "CastTransport.h"

using namespace campiello::cast;

static int gFail = 0;
#define CHECK(cond) do { if (!(cond)) { \
	std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++gFail; } } while (0)

static std::string FromHex(const std::string& hex)
{
	std::string out;
	for (size_t i = 0; i + 1 < hex.size(); i += 2)
		out += static_cast<char>(std::strtol(hex.substr(i, 2).c_str(), nullptr, 16));
	return out;
}
static std::string ToHex(const std::string& in)
{
	static const char* h = "0123456789abcdef";
	std::string out;
	for (unsigned char c : in) { out += h[c >> 4]; out += h[c & 0xf]; }
	return out;
}

int main()
{
	// AES-128-CTR known-answer (NIST SP800-38A, F.5.1 CTR-AES128.Encrypt, block 1).
	{
		std::string key = FromHex("2b7e151628aed2a6abf7158809cf4f3c");
		std::string ctr = FromHex("f0f1f2f3f4f5f6f7f8f9fafbfcfdfeff");
		std::string pt  = FromHex("6bc1bee22e409f96e93d7e117393172a");
		FrameCrypto crypto(reinterpret_cast<const uint8_t*>(key.data()),
			reinterpret_cast<const uint8_t*>(ctr.data()));
		// frameId 0 leaves the nonce == the initial counter block.
		std::string ct = crypto.Encrypt(0, pt);
		CHECK(ToHex(ct) == "874d6191b620e3261bef6864990db6ce");
	}

	// Nonce derivation: frame id XORed big-endian into the first 4 bytes of the mask.
	{
		uint8_t mask[16]; std::memset(mask, 0, sizeof(mask));
		uint8_t iv[16];
		CastNonce(0x01020304u, mask, iv);
		// Cast writes the 32-bit frame_id big-endian into bytes [8..11].
		CHECK(iv[8] == 0x01 && iv[9] == 0x02 && iv[10] == 0x03 && iv[11] == 0x04);
		for (int i = 0; i < 16; ++i)
			if (i < 8 || i > 11) CHECK(iv[i] == 0);
	}

	// Encrypt/decrypt round-trip with a non-zero frame id.
	{
		uint8_t key[16], mask[16];
		for (int i = 0; i < 16; ++i) { key[i] = i; mask[i] = 0xA0 + i; }
		FrameCrypto crypto(key, mask);
		std::string plain = "Campiello mirrors the Haiku desktop over Cast Streaming.";
		std::string enc = crypto.Encrypt(42, plain);
		CHECK(enc.size() == plain.size());
		CHECK(enc != plain);
		CHECK(crypto.Decrypt(42, enc) == plain);
		// Wrong frame id must NOT decrypt back to the plaintext.
		CHECK(crypto.Decrypt(43, enc) != plain);
	}

	// RTP header round-trip.
	{
		RtpHeader h;
		h.payloadType = 127; h.sequenceNumber = 40000; h.rtpTimestamp = 0x11223344;
		h.ssrc = 100000; h.keyFrame = true; h.frameId = 7; h.packetId = 3; h.maxPacketId = 9;
		h.hasReference = true; h.referenceFrameId = 6;
		std::string pkt = BuildRtpPacket(h, "hello");
		RtpHeader g; std::string payload;
		CHECK(ParseRtpPacket(pkt, g, payload));
		CHECK(g.payloadType == 127);
		CHECK(g.sequenceNumber == 40000);
		CHECK(g.rtpTimestamp == 0x11223344u);
		CHECK(g.ssrc == 100000u);
		CHECK(g.keyFrame);
		CHECK(g.frameId == 7);
		CHECK(g.packetId == 3);
		CHECK(g.maxPacketId == 9);
		CHECK(g.hasReference);
		CHECK(g.referenceFrameId == 6);
		CHECK(payload == "hello");
	}

	// Packetize a frame and reassemble it (in order, and shuffled).
	{
		std::string frame;
		for (int i = 0; i < 3500; ++i) frame += static_cast<char>((i * 7) & 0xff);
		auto pkts = Packetize(100000, 127, 1000, 5, true, 0xDEADBEEF, false, 0, frame, 1400);
		CHECK(pkts.size() == 3); // 3500 / (1400-18) -> 3 packets
		// every packet carries the key bit and the right maxPacketId
		for (const auto& p : pkts) {
			RtpHeader h; std::string pl;
			CHECK(ParseRtpPacket(p, h, pl));
			CHECK(h.keyFrame);
			CHECK(h.maxPacketId == 2);
			CHECK(h.frameId == 5);
			CHECK(h.ssrc == 100000u);
		}
		CHECK(Reassemble(pkts) == frame);
		std::vector<std::string> shuffled = {pkts[2], pkts[0], pkts[1]};
		CHECK(Reassemble(shuffled) == frame);
		// A missing packet fails cleanly.
		std::vector<std::string> missing = {pkts[0], pkts[2]};
		CHECK(Reassemble(missing).empty());
	}

	// A tiny frame is one packet.
	{
		auto pkts = Packetize(1, 96, 0, 0, false, 0, false, 0, "abc", 1400);
		CHECK(pkts.size() == 1);
		RtpHeader h; std::string pl; ParseRtpPacket(pkts[0], h, pl);
		CHECK(h.maxPacketId == 0 && pl == "abc" && !h.keyFrame);
	}

	// UDP sender over loopback.
	{
		int rfd = socket(AF_INET, SOCK_DGRAM, 0);
		CHECK(rfd >= 0);
		sockaddr_in a; std::memset(&a, 0, sizeof(a));
		a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_LOOPBACK); a.sin_port = 0;
		CHECK(bind(rfd, (sockaddr*)&a, sizeof(a)) == 0);
		socklen_t al = sizeof(a);
		getsockname(rfd, (sockaddr*)&a, &al);
		int port = ntohs(a.sin_port);

		UdpSender snd;
		CHECK(snd.Open("127.0.0.1", port));
		std::string datagram = BuildRtpPacket(RtpHeader{}, "payload-bytes");
		CHECK(snd.Send(datagram));

		char buf[2048];
		ssize_t n = recv(rfd, buf, sizeof(buf), 0);
		CHECK(n == (ssize_t)datagram.size());
		CHECK(std::string(buf, n) == datagram);
		close(rfd);
	}

	// RTCP sender report layout.
	{
		std::string sr = BuildSenderReport(100000, 0x11111111, 0x22222222, 0x33333333, 10, 2000);
		CHECK(sr.size() == 28); // 7 words
		CHECK((unsigned char)sr[0] == 0x80);
		CHECK((unsigned char)sr[1] == 200); // PT = SR
		CHECK((unsigned char)sr[2] == 0x00 && (unsigned char)sr[3] == 0x06); // length = 6
	}

	if (gFail == 0)
		std::printf("all Cast transport tests passed\n");
	return gFail == 0 ? 0 : 1;
}
