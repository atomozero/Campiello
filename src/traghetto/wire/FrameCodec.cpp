// FrameCodec.cpp
//
// Implementation of the CNP frame encoder and the incremental decoder. See FrameCodec.h.

#include "FrameCodec.h"

namespace campiello {
namespace wire {

namespace {

// Read a big-endian u32 from four bytes.
uint32_t ReadU32BE(const uint8_t* p)
{
	return (static_cast<uint32_t>(p[0]) << 24)
	     | (static_cast<uint32_t>(p[1]) << 16)
	     | (static_cast<uint32_t>(p[2]) << 8)
	     |  static_cast<uint32_t>(p[3]);
}

// Append a big-endian u32.
void WriteU32BE(std::vector<uint8_t>& out, uint32_t v)
{
	out.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
	out.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
	out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
	out.push_back(static_cast<uint8_t>(v & 0xFF));
}

} // namespace

bool EncodeFrame(const Frame& frame, std::vector<uint8_t>& out)
{
	if (frame.payload.size() > kMaxPayloadLength)
		return false;

	out.reserve(out.size() + kHeaderSize + frame.payload.size());
	out.push_back(kMagic0);
	out.push_back(kMagic1);
	out.push_back(frame.version);
	out.push_back(static_cast<uint8_t>(frame.type));
	WriteU32BE(out, frame.requestId);
	WriteU32BE(out, static_cast<uint32_t>(frame.payload.size()));
	out.insert(out.end(), frame.payload.begin(), frame.payload.end());
	return true;
}

std::vector<uint8_t> EncodeFrame(const Frame& frame)
{
	std::vector<uint8_t> out;
	if (!EncodeFrame(frame, out))
		out.clear();
	return out;
}

void FrameParser::Feed(const uint8_t* data, size_t length)
{
	// Reclaim consumed bytes so the buffer does not grow without bound across many
	// frames. Compacting on Feed keeps Next allocation-free in the common path.
	if (fReadPos > 0) {
		fBuffer.erase(fBuffer.begin(), fBuffer.begin() + fReadPos);
		fReadPos = 0;
	}
	if (length > 0)
		fBuffer.insert(fBuffer.end(), data, data + length);
}

void FrameParser::Latch(const char* message)
{
	fError = true;
	fErrorMessage = message;
}

ParseResult FrameParser::Next(Frame& out)
{
	if (fError)
		return ParseResult::kError;

	const size_t available = fBuffer.size() - fReadPos;
	if (available < kHeaderSize)
		return ParseResult::kNeedMore;

	const uint8_t* h = fBuffer.data() + fReadPos;

	if (h[0] != kMagic0 || h[1] != kMagic1) {
		Latch("bad magic");
		return ParseResult::kError;
	}

	const uint8_t version = h[2];
	if (version != kProtocolVersion) {
		Latch("unsupported protocol version");
		return ParseResult::kError;
	}

	const uint32_t payloadLen = ReadU32BE(h + 8);
	if (payloadLen > kMaxPayloadLength) {
		// Reject before allocating anything of that size.
		Latch("payload length exceeds maximum");
		return ParseResult::kError;
	}

	const size_t frameSize = kHeaderSize + payloadLen;
	if (available < frameSize)
		return ParseResult::kNeedMore;

	out.version = version;
	out.type = static_cast<MessageType>(h[3]);
	out.requestId = ReadU32BE(h + 4);
	out.payload.assign(h + kHeaderSize, h + frameSize);

	fReadPos += frameSize;
	return ParseResult::kFrame;
}

} // namespace wire
} // namespace campiello
