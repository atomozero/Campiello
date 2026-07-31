// Error.h
//
// CNP ERROR reply. Any request that fails comes back as an ERROR frame (type 0xE0) whose
// request_id matches the failed request. The body carries a stable machine code plus an
// optional developer-facing message.
//
// Per the error-surface rule (docs/PROPOSAL.md section 13), user-visible text is short and
// human and is rendered by the client from the code (localized to Italian for end users);
// the `msg` field is developer detail for logs, not a string to show verbatim. Codes are
// our own stable wire values, not raw errno, and unknown codes are tolerated by decoders
// (mapped to a generic failure) for forward compatibility.
//
// Pure standard C++ (no Haiku dependency), built on Cbor.h / Frame.h.

#ifndef CAMPIELLO_TRAGHETTO_WIRE_ERROR_H
#define CAMPIELLO_TRAGHETTO_WIRE_ERROR_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "Frame.h"

namespace campiello {
namespace wire {

// Longest developer message carried in an ERROR (untrusted-input guard, rule 7).
static const size_t kMaxErrorMessageBytes = 256;

// Stable wire error codes. Decoders accept unknown values too.
enum class ErrorCode : uint32_t {
	kInternal       = 1,  // unexpected server-side failure
	kNotFound       = 2,  // path does not exist
	kAccessDenied   = 3,  // outside the shared root, or read-only, or not permitted
	kNotADirectory  = 4,
	kIsADirectory   = 5,
	kInvalidRequest = 6,  // malformed payload or invalid path
	kUnsupported    = 7,  // operation not supported by this peer
	kIOError        = 8,
	kNoSpace        = 9,
	kBadHandle      = 10, // unknown or already-closed handle
	kTooManyOpen    = 11, // open-handle limit reached
};

struct ErrorReply {
	uint32_t code = 0;      // an ErrorCode value; kept as uint so unknown codes survive
	std::string message;    // "msg", developer detail (may be empty)
};

// Encode an ERROR body. The message is optional; an empty message omits the "msg" key.
std::vector<uint8_t> EncodeError(ErrorCode code, const std::string& message = "");

// Decode an ERROR body. `code` is mandatory (any uint32 accepted); `msg` is optional and
// bounded. Rejects malformed input, a duplicate key, an oversized message, or trailing
// bytes; unknown keys are skipped.
bool DecodeError(const std::vector<uint8_t>& payload, ErrorReply& out);

// Build a complete ERROR frame echoing the failed request's id.
Frame MakeError(ErrorCode code, const std::string& message, uint32_t requestId);

} // namespace wire
} // namespace campiello

#endif // CAMPIELLO_TRAGHETTO_WIRE_ERROR_H
