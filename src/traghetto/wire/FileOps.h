// FileOps.h
//
// CNP file read path: OPEN, READ, CLOSE. Request and reply share a message type and
// request_id; a failure comes back as an ERROR frame instead.
//
//   OPEN  (0x12): request { mode, path } -> reply { size, handle }
//   READ  (0x13): request { handle, length, offset } -> reply { data }
//   WRITE (0x14): request { data, handle, offset } -> reply { written }
//   CLOSE (0x15): request { handle } -> reply Ok (empty map)
//
// The handle is an opaque uint64 the responder assigns at OPEN and the requester passes
// back to READ, WRITE, and CLOSE. A READ reply shorter than the requested length signals
// EOF. WRITE needs a handle opened with kOpenWrite; the reply's `written` is the byte count
// actually stored (normally the full data length).
//
// Pure standard C++ (no Haiku dependency), built on Cbor.h / Frame.h. Reuses the path
// bound from Listing.h.

#ifndef CAMPIELLO_TRAGHETTO_WIRE_FILEOPS_H
#define CAMPIELLO_TRAGHETTO_WIRE_FILEOPS_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "Frame.h"
#include "Listing.h" // kMaxPathBytes

namespace campiello {
namespace wire {

// Open-mode bitmask. Extensible; the read path uses kOpenRead. Create/truncate come with
// the write path (PROPOSAL.md section 8, phase 2).
static const uint32_t kOpenRead  = 0x1;
static const uint32_t kOpenWrite = 0x2;

// Largest byte count a single READ may request or a READ reply may carry. Bounds memory
// and keeps a read chunk well under the 16 MiB frame cap (untrusted-input guard, rule 7).
static const size_t kMaxReadLength = 1u << 20; // 1 MiB

// Largest byte count a single WRITE may carry, same bound and rationale as reads.
static const size_t kMaxWriteLength = 1u << 20; // 1 MiB

// OPEN.
std::vector<uint8_t> EncodeOpenRequest(const std::string& path, uint32_t mode);
bool DecodeOpenRequest(const std::vector<uint8_t>& payload, std::string& path, uint32_t& mode);
std::vector<uint8_t> EncodeOpenReply(uint64_t handle, uint64_t size);
bool DecodeOpenReply(const std::vector<uint8_t>& payload, uint64_t& handle, uint64_t& size);

// READ. A reply carries up to kMaxReadLength bytes; fewer than requested means EOF.
std::vector<uint8_t> EncodeReadRequest(uint64_t handle, uint64_t offset, uint32_t length);
bool DecodeReadRequest(const std::vector<uint8_t>& payload, uint64_t& handle,
	uint64_t& offset, uint32_t& length);
std::vector<uint8_t> EncodeReadReply(const std::vector<uint8_t>& data);
bool DecodeReadReply(const std::vector<uint8_t>& payload, std::vector<uint8_t>& data);

// WRITE. A request carries up to kMaxWriteLength bytes; the reply's `written` is how many
// were stored.
std::vector<uint8_t> EncodeWriteRequest(uint64_t handle, uint64_t offset,
	const std::vector<uint8_t>& data);
bool DecodeWriteRequest(const std::vector<uint8_t>& payload, uint64_t& handle,
	uint64_t& offset, std::vector<uint8_t>& data);
std::vector<uint8_t> EncodeWriteReply(uint64_t written);
bool DecodeWriteReply(const std::vector<uint8_t>& payload, uint64_t& written);

// CLOSE.
std::vector<uint8_t> EncodeCloseRequest(uint64_t handle);
bool DecodeCloseRequest(const std::vector<uint8_t>& payload, uint64_t& handle);

// Ok ack: an empty map. Reusable for any reply that only needs to confirm success (CLOSE
// today). Decode tolerates extra fields for forward compatibility.
std::vector<uint8_t> EncodeOk();
bool DecodeOk(const std::vector<uint8_t>& payload);

// Frame builders. Replies echo the request's requestId.
Frame MakeOpenRequest(const std::string& path, uint32_t mode, uint32_t requestId);
Frame MakeOpenReply(uint64_t handle, uint64_t size, uint32_t requestId);
Frame MakeReadRequest(uint64_t handle, uint64_t offset, uint32_t length, uint32_t requestId);
Frame MakeReadReply(const std::vector<uint8_t>& data, uint32_t requestId);
Frame MakeWriteRequest(uint64_t handle, uint64_t offset, const std::vector<uint8_t>& data,
	uint32_t requestId);
Frame MakeWriteReply(uint64_t written, uint32_t requestId);
Frame MakeCloseRequest(uint64_t handle, uint32_t requestId);
Frame MakeCloseReply(uint32_t requestId);

} // namespace wire
} // namespace campiello

#endif // CAMPIELLO_TRAGHETTO_WIRE_FILEOPS_H
