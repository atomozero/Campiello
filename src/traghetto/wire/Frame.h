// Frame.h
//
// Campiello Native Protocol (CNP) frame envelope: the fixed 12-byte header plus an
// opaque payload. This is the outer framing only; the payload bytes are a CBOR-encoded
// message body that a higher layer decodes. The framing layer never interprets the
// payload.
//
// Wire layout (big-endian, network order), see docs/PROTOCOL.md:
//
//   offset  size  field
//   0       2     magic       'C','N' (0x43 0x4E)
//   2       1     version     protocol version (currently 1)
//   3       1     type        MessageType (carried verbatim, not validated here)
//   4       4     request_id  u32, echoed in replies; enables pipelining
//   8       4     payload_len u32, length of the payload that follows
//   12      N     payload     payload_len bytes (opaque CBOR)
//
// Pure standard C++ (no Haiku dependency) so the wire logic is unit-testable off Haiku,
// per the portable-core pattern in docs/REUSE.md and the testing strategy in
// docs/PROPOSAL.md section 15.
//
// STATUS: provisional. The constants and MessageType values below are frozen during M2
// (docs/PROPOSAL.md section 18); keep them in sync with docs/PROTOCOL.md and the golden
// tests in tests/wire/.

#ifndef CAMPIELLO_TRAGHETTO_WIRE_FRAME_H
#define CAMPIELLO_TRAGHETTO_WIRE_FRAME_H

#include <cstdint>
#include <vector>

namespace campiello {
namespace wire {

// First magic byte: 'C'.
static const uint8_t kMagic0 = 0x43;
// Second magic byte: 'N'.
static const uint8_t kMagic1 = 0x4E;

// Current protocol version. A peer announcing a different version is rejected at the
// framing layer; capability/version negotiation itself rides in the HELLO payload.
static const uint8_t kProtocolVersion = 0x01;

// Fixed header size in bytes: magic(2) + version(1) + type(1) + request_id(4) + len(4).
static const size_t kHeaderSize = 12;

// Upper bound on a single frame's payload. A declared payload_len above this is a
// protocol error and is rejected without allocating, since peers are untrusted
// (docs/PROPOSAL.md working agreement rule 7).
static const uint32_t kMaxPayloadLength = 16u * 1024u * 1024u; // 16 MiB

// Message types carried in the header `type` byte. Grouped by nibble. The framing layer
// passes this through verbatim; unknown values are a higher-layer concern so the
// protocol can gain message types without a framing change.
enum class MessageType : uint8_t {
	kInvalid     = 0x00,

	// Handshake.
	kHello       = 0x01,
	kWelcome     = 0x02,

	// File and directory IO.
	kList        = 0x10,
	kStat        = 0x11,
	kOpen        = 0x12,
	kRead        = 0x13,
	kWrite       = 0x14,
	kClose       = 0x15,

	// Namespace mutation.
	kMkdir       = 0x20,
	kUnlink      = 0x21,
	kRename      = 0x22,
	kTruncate    = 0x23,

	// BFS extended attributes.
	kListAttrs   = 0x30,
	kReadAttrs   = 0x31,
	kWriteAttrs  = 0x32,

	// Distributed live queries.
	kQueryOpen   = 0x40,
	kQueryResult = 0x41,
	kQueryUpdate = 0x42,
	kQueryClose  = 0x43,

	// Error reply.
	kError       = 0xE0,
};

// One decoded frame. `payload` holds the raw (still CBOR-encoded) body.
struct Frame {
	uint8_t     version = kProtocolVersion;
	MessageType type    = MessageType::kInvalid;
	uint32_t    requestId = 0;
	std::vector<uint8_t> payload;
};

} // namespace wire
} // namespace campiello

#endif // CAMPIELLO_TRAGHETTO_WIRE_FRAME_H
