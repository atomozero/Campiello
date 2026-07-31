// Attributes.h
//
// CNP AttrSet: a node's BFS extended attributes carried with full type fidelity. Reused
// by STAT/LIST entries, READ_ATTRS/WRITE_ATTRS, and query results. Each attribute is a
// CBOR map { n: name, t: type_code, v: raw bytes }; an AttrSet is a CBOR array of them.
//
// The type_code travels as a CBOR unsigned integer (the 32-bit value of the Haiku
// B_*_TYPE FourCC, e.g. B_STRING_TYPE = 'CSTR' = 0x43535452). The attribute size is
// implicit in the byte-string length, so it is not a separate field. On receipt the value
// is written back verbatim with its type via BNode::WriteAttr / fs_write_attr, preserving
// MIME type, icon, ratings, and app-specific attributes (docs/VERIFIED.md section 3).
//
// Pure standard C++ (no Haiku dependency), built on Cbor.h.

#ifndef CAMPIELLO_TRAGHETTO_WIRE_ATTRIBUTES_H
#define CAMPIELLO_TRAGHETTO_WIRE_ATTRIBUTES_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "Cbor.h"

namespace campiello {
namespace wire {

// Longest attribute name, B_ATTR_NAME_LENGTH = B_FILE_NAME_LENGTH - 1 = 255
// (verified against headers/os/storage/StorageDefs.h and headers/posix/limits.h).
static const size_t kMaxAttrNameBytes = 255;

// Upper bound on attributes carried in one set (untrusted-input guard, rule 7).
static const size_t kMaxAttrs = 4096;

// One typed BFS attribute.
struct Attr {
	std::string name;              // "n", 1..kMaxAttrNameBytes bytes
	uint32_t type = 0;             // "t", Haiku type_code
	std::vector<uint8_t> value;    // "v", raw bytes (may be empty)
};

using AttrSet = std::vector<Attr>;

// Write an AttrSet as a CBOR array into an existing writer (composes inside an Entry).
void WriteAttrSet(CborWriter& w, const AttrSet& attrs);

// Read a CBOR array of attributes from an existing reader. Returns false on malformed
// input, an empty or oversized name, a duplicate key within an attribute, a count over
// kMaxAttrs, or any missing mandatory field. Does not check end-of-stream; the caller
// does that at the top level.
bool ReadAttrSet(CborReader& r, AttrSet& out);

} // namespace wire
} // namespace campiello

#endif // CAMPIELLO_TRAGHETTO_WIRE_ATTRIBUTES_H
