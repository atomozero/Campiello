// AttrOps.h
//
// CNP attribute messages: READ_ATTRS and WRITE_ATTRS, the dedicated path for BFS extended
// attributes when a full STAT/LIST Entry is not needed. Both carry an AttrSet with full type
// fidelity (Attributes.h). WRITE_ATTRS is the headline M3 capability: attributes copied
// Campiello to Campiello keep their real type codes.
//
//   READ_ATTRS  (0x31): request { path }         -> reply { attrs: AttrSet }
//   WRITE_ATTRS (0x32): request { path, attrs }  -> reply Ok
//
// WRITE_ATTRS replaces the named attributes on the target; the responder validates each
// attribute name and enforces the shared-root boundary and per-peer read-only default before
// writing. LIST_ATTRS (0x30) is intentionally not implemented yet: STAT/LIST already carry
// the full AttrSet, so name-only enumeration is deferred until a caller needs it.
//
// Pure standard C++ (no Haiku dependency), built on Attributes.h / Cbor.h / Frame.h. Reuses
// the { path } body from Listing.h and the Ok ack from FileOps.h.

#ifndef CAMPIELLO_TRAGHETTO_WIRE_ATTROPS_H
#define CAMPIELLO_TRAGHETTO_WIRE_ATTROPS_H

#include <cstdint>
#include <string>
#include <vector>

#include "Attributes.h"
#include "Frame.h"
#include "Listing.h" // kMaxPathBytes, EncodePathRequest / DecodePathRequest

namespace campiello {
namespace wire {

// READ_ATTRS request is a bare { path }; reuse EncodePathRequest / DecodePathRequest.

// READ_ATTRS reply { attrs: AttrSet }.
std::vector<uint8_t> EncodeReadAttrsReply(const AttrSet& attrs);
bool DecodeReadAttrsReply(const std::vector<uint8_t>& payload, AttrSet& out);

// WRITE_ATTRS request { path, attrs }.
std::vector<uint8_t> EncodeWriteAttrsRequest(const std::string& path, const AttrSet& attrs);
bool DecodeWriteAttrsRequest(const std::vector<uint8_t>& payload, std::string& path,
	AttrSet& attrs);

// Frame builders. Replies echo the request's requestId.
Frame MakeReadAttrsRequest(const std::string& path, uint32_t requestId);
Frame MakeReadAttrsReply(const AttrSet& attrs, uint32_t requestId);
Frame MakeWriteAttrsRequest(const std::string& path, const AttrSet& attrs, uint32_t requestId);
Frame MakeWriteAttrsReply(uint32_t requestId);

} // namespace wire
} // namespace campiello

#endif // CAMPIELLO_TRAGHETTO_WIRE_ATTROPS_H
