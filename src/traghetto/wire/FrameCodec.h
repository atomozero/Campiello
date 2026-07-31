// FrameCodec.h
//
// Encode CNP frames to bytes, and decode a byte stream back into frames incrementally.
// TCP delivers a stream with no message boundaries, so FrameParser accumulates fed bytes
// and yields whole frames as they complete. The incremental Feed/Next discipline mirrors
// the buffer pattern harvested from Sotoportego (docs/REUSE.md), rewritten for a fixed
// header plus length prefix instead of newline splitting.
//
// Pure standard C++ (no Haiku dependency). See Frame.h for the wire layout.

#ifndef CAMPIELLO_TRAGHETTO_WIRE_FRAMECODEC_H
#define CAMPIELLO_TRAGHETTO_WIRE_FRAMECODEC_H

#include <cstdint>
#include <vector>

#include "Frame.h"

namespace campiello {
namespace wire {

// Serialize `frame` into `out` (appended). Returns false without touching `out` if the
// payload exceeds kMaxPayloadLength; callers must split or reject oversized bodies.
bool EncodeFrame(const Frame& frame, std::vector<uint8_t>& out);

// Convenience wrapper returning a fresh buffer. On oversize, returns an empty vector.
std::vector<uint8_t> EncodeFrame(const Frame& frame);

// Result of pulling from the parser.
enum class ParseResult {
	kFrame,     // a complete frame was written to `out`
	kNeedMore,  // header/payload not fully arrived yet; feed more bytes
	kError,     // protocol violation; the parser is now latched in error, drop the peer
};

// Incremental, stream-oriented frame decoder. Not thread-safe; drive it from one reader.
//
// Usage:
//   parser.Feed(chunk, n);
//   Frame f;
//   ParseResult r;
//   while ((r = parser.Next(f)) == ParseResult::kFrame) { dispatch(f); }
//   if (r == ParseResult::kError) { drop_connection(); }
//
// Untrusted-input guarantees: a bad magic, an unsupported version, or a payload_len over
// kMaxPayloadLength latches the parser into kError (Next keeps returning kError) and
// never allocates the oversized amount. A merely truncated frame returns kNeedMore, not
// kError, so partial reads are safe.
class FrameParser {
public:
	FrameParser() = default;

	// Append raw bytes to the internal buffer. Reclaims already-consumed bytes first, so
	// steady-state memory stays bounded by the largest in-flight frame.
	void Feed(const uint8_t* data, size_t length);

	// Try to extract one frame from the buffered bytes. See ParseResult.
	ParseResult Next(Frame& out);

	// True once the parser has latched an error. It will not recover.
	bool HasError() const { return fError; }

	// Human-readable reason for the latched error, or nullptr if none. Developer log
	// only; never surfaced to the user (docs/PROPOSAL.md section 13).
	const char* ErrorMessage() const { return fError ? fErrorMessage : nullptr; }

	// Bytes buffered but not yet consumed into a frame. Test/introspection helper.
	size_t Buffered() const { return fBuffer.size() - fReadPos; }

private:
	void Latch(const char* message);

	std::vector<uint8_t> fBuffer;
	size_t      fReadPos = 0;
	bool        fError = false;
	const char* fErrorMessage = nullptr;
};

} // namespace wire
} // namespace campiello

#endif // CAMPIELLO_TRAGHETTO_WIRE_FRAMECODEC_H
