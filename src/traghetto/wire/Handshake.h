// Handshake.h
//
// CNP HELLO / WELCOME handshake payloads. Both messages carry the same body, a
// NodeIdentity; the frame `type` (kHello / kWelcome) distinguishes them. HELLO is sent by
// the initiator with the highest protocol version it speaks; WELCOME is the responder's
// reply carrying the negotiated version.
//
// The body is a CBOR map (see docs/PROTOCOL.md). Encoding is canonical (length-first key
// order v, fp, caps, node) for deterministic bytes; decoding is order-independent and
// strict against untrusted input (docs/PROPOSAL.md rule 7).
//
// Pure standard C++ (no Haiku dependency), built on the wire codec in Cbor.h / Frame.h.

#ifndef CAMPIELLO_TRAGHETTO_WIRE_HANDSHAKE_H
#define CAMPIELLO_TRAGHETTO_WIRE_HANDSHAKE_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "Frame.h"

namespace campiello {
namespace wire {

// Length of an identity fingerprint: SHA-256 over the SPKI (i2d_PUBKEY), see
// docs/VERIFIED.md section 6. Exactly 32 bytes.
static const size_t kFingerprintBytes = 32;

// Untrusted-input bounds for the handshake body.
static const size_t kMaxNodeNameBytes = 128;
static const size_t kMaxCaps = 32;
static const size_t kMaxCapBytes = 32;

// Known capability token: native BFS attribute fidelity plus live queries. The caps
// vocabulary is open; unknown tokens are ignored, not rejected.
constexpr const char kCapBfs[] = "bfs";

// Identity a node announces in HELLO / WELCOME.
struct NodeIdentity {
	uint32_t version = kProtocolVersion; // "v"
	std::vector<uint8_t> fingerprint;    // "fp", SPKI SHA-256, must be kFingerprintBytes
	std::vector<std::string> caps;       // "caps", capability tokens
	std::string node;                    // "node", friendly label (attacker-controlled)

	bool HasCap(const std::string& cap) const;
};

// Encode the NodeIdentity body to a CBOR payload (canonical key order).
std::vector<uint8_t> EncodeNodeIdentity(const NodeIdentity& id);

// Decode a CBOR handshake body. Returns false (leaving `out` unspecified) on any
// malformed input, a missing/duplicate mandatory field, an out-of-bounds value, or
// trailing bytes after the map. Unknown keys are skipped for forward compatibility.
bool DecodeNodeIdentity(const uint8_t* data, size_t length, NodeIdentity& out);
bool DecodeNodeIdentity(const std::vector<uint8_t>& payload, NodeIdentity& out);

// Build complete HELLO / WELCOME frames. `requestId` is the initiator's id; WELCOME
// should echo the HELLO's requestId.
Frame MakeHello(const NodeIdentity& id, uint32_t requestId = 0);
Frame MakeWelcome(const NodeIdentity& id, uint32_t requestId);

} // namespace wire
} // namespace campiello

#endif // CAMPIELLO_TRAGHETTO_WIRE_HANDSHAKE_H
