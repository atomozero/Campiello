// Namespace.h
//
// CNP namespace-mutation messages: MKDIR, UNLINK, RENAME, TRUNCATE. Each request is answered
// with an Ok ack (empty map) on success, or an ERROR frame on failure. These are the write
// path's directory operations (PROPOSAL.md section 8, phase 2); the responder enforces the
// shared-root boundary and the per-peer read-only default before acting.
//
//   MKDIR    (0x20): request { mode, path }  -> Ok
//   UNLINK   (0x21): request { path }        -> Ok   (removes a file or an empty directory)
//   RENAME   (0x22): request { to, from }    -> Ok   (both paths inside the shared root)
//   TRUNCATE (0x23): request { path, size }  -> Ok
//
// Pure standard C++ (no Haiku dependency), built on Cbor.h / Frame.h. Reuses the path bound
// and the { path } body from Listing.h and the Ok ack from FileOps.h.

#ifndef CAMPIELLO_TRAGHETTO_WIRE_NAMESPACE_H
#define CAMPIELLO_TRAGHETTO_WIRE_NAMESPACE_H

#include <cstdint>
#include <string>
#include <vector>

#include "Frame.h"
#include "Listing.h" // kMaxPathBytes, EncodePathRequest / DecodePathRequest

namespace campiello {
namespace wire {

// MKDIR request { mode, path }.
std::vector<uint8_t> EncodeMkdirRequest(const std::string& path, uint32_t mode);
bool DecodeMkdirRequest(const std::vector<uint8_t>& payload, std::string& path, uint32_t& mode);

// UNLINK request is a bare { path }; reuse EncodePathRequest / DecodePathRequest.

// RENAME request { to, from }.
std::vector<uint8_t> EncodeRenameRequest(const std::string& from, const std::string& to);
bool DecodeRenameRequest(const std::vector<uint8_t>& payload, std::string& from, std::string& to);

// TRUNCATE request { path, size }.
std::vector<uint8_t> EncodeTruncateRequest(const std::string& path, uint64_t size);
bool DecodeTruncateRequest(const std::vector<uint8_t>& payload, std::string& path, uint64_t& size);

// Frame builders. Every reply is an Ok ack echoing the request's requestId.
Frame MakeMkdirRequest(const std::string& path, uint32_t mode, uint32_t requestId);
Frame MakeUnlinkRequest(const std::string& path, uint32_t requestId);
Frame MakeRenameRequest(const std::string& from, const std::string& to, uint32_t requestId);
Frame MakeTruncateRequest(const std::string& path, uint64_t size, uint32_t requestId);
Frame MakeMkdirReply(uint32_t requestId);
Frame MakeUnlinkReply(uint32_t requestId);
Frame MakeRenameReply(uint32_t requestId);
Frame MakeTruncateReply(uint32_t requestId);

} // namespace wire
} // namespace campiello

#endif // CAMPIELLO_TRAGHETTO_WIRE_NAMESPACE_H
