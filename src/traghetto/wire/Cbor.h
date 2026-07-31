// Cbor.h
//
// Minimal hand-rolled CBOR (RFC 8949) codec for CNP message payloads. Deliberately a
// subset, not a full implementation: it covers exactly what the protocol needs.
//
// Supported major types:
//   0 unsigned int, 1 negative int, 2 byte string, 3 text string,
//   4 array, 5 map, and major 7 limited to false / true / null.
// Definite lengths only. Encoding is canonical (shortest form), so output is
// deterministic for golden tests and future signing.
//
// Rejected on decode (treated as errors): floats (major 7 half/single/double), tags
// (major 6), indefinite-length items (additional info 31), and the reserved additional
// info values 28-30.
//
// Untrusted-input posture (docs/PROPOSAL.md working agreement rule 7): the reader never
// reads past the buffer; a declared string/array/map length beyond the remaining bytes
// is an error and never causes an oversized allocation; Skip() is iterative so hostile
// nesting cannot overflow the stack.
//
// Pure standard C++ (no Haiku dependency), unit-testable off Haiku per PROPOSAL.md
// section 15. Map key ordering is the caller's responsibility; this layer does not
// enforce canonical key order.

#ifndef CAMPIELLO_TRAGHETTO_WIRE_CBOR_H
#define CAMPIELLO_TRAGHETTO_WIRE_CBOR_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace campiello {
namespace wire {

// Builds a CBOR byte string. Container headers (ArrayHeader/MapHeader) declare a count;
// the caller then writes exactly that many items (or key/value pairs).
class CborWriter {
public:
	void UInt(uint64_t v);
	void Int(int64_t v);
	void Bytes(const uint8_t* data, size_t length);
	void Bytes(const std::vector<uint8_t>& b);
	void Text(const std::string& s);
	void Text(const char* s);
	void Bool(bool b);
	void Null();
	void ArrayHeader(size_t count);
	void MapHeader(size_t count);

	const std::vector<uint8_t>& Buffer() const { return fBuf; }
	std::vector<uint8_t> Take() { return std::move(fBuf); }
	size_t Size() const { return fBuf.size(); }

private:
	void WriteTypeAndArg(uint8_t majorType, uint64_t arg);
	std::vector<uint8_t> fBuf;
};

// Walks a CBOR byte string. One reader per buffer; not thread-safe. A malformed item or
// a type mismatch latches the reader into an error state (subsequent reads fail).
class CborReader {
public:
	CborReader(const uint8_t* data, size_t length) : fData(data), fLen(length) {}
	explicit CborReader(const std::vector<uint8_t>& b) : fData(b.data()), fLen(b.size()) {}

	enum class Type {
		kUInt, kNegInt, kBytes, kText, kArray, kMap, kBool, kNull, kInvalid
	};

	// Classify the next item without consuming it. kInvalid at end, on error, or for an
	// unsupported item (float, tag, indefinite).
	Type Peek() const;

	bool ReadUInt(uint64_t& out);
	bool ReadInt(int64_t& out);            // accepts major 0 or 1, must fit in int64
	bool ReadBytes(std::vector<uint8_t>& out);
	bool ReadText(std::string& out);
	bool ReadBool(bool& out);
	bool ReadNull();
	bool ReadArrayHeader(uint64_t& count); // then read `count` items
	bool ReadMapHeader(uint64_t& count);   // then read `count` key/value pairs

	// Skip one complete item (recursively for containers), for ignoring unknown map keys.
	bool Skip();

	bool   AtEnd() const { return !fError && fPos >= fLen; }
	bool   HasError() const { return fError; }
	size_t Offset() const { return fPos; }

private:
	bool ReadHead(uint8_t& majorType, uint64_t& arg); // majors 0-6; rejects indefinite
	void Fail() { fError = true; }

	const uint8_t* fData;
	size_t         fLen;
	size_t         fPos = 0;
	bool           fError = false;
};

} // namespace wire
} // namespace campiello

#endif // CAMPIELLO_TRAGHETTO_WIRE_CBOR_H
