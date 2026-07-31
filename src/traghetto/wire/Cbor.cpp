// Cbor.cpp
//
// Implementation of the minimal CBOR codec. See Cbor.h.

#include "Cbor.h"

#include <cstring>

namespace campiello {
namespace wire {

// ---- Writer ---------------------------------------------------------------------------

void CborWriter::WriteTypeAndArg(uint8_t majorType, uint64_t arg)
{
	const uint8_t hi = static_cast<uint8_t>(majorType << 5);
	if (arg < 24) {
		fBuf.push_back(static_cast<uint8_t>(hi | arg));
	} else if (arg <= 0xFF) {
		fBuf.push_back(static_cast<uint8_t>(hi | 24));
		fBuf.push_back(static_cast<uint8_t>(arg));
	} else if (arg <= 0xFFFF) {
		fBuf.push_back(static_cast<uint8_t>(hi | 25));
		fBuf.push_back(static_cast<uint8_t>((arg >> 8) & 0xFF));
		fBuf.push_back(static_cast<uint8_t>(arg & 0xFF));
	} else if (arg <= 0xFFFFFFFFull) {
		fBuf.push_back(static_cast<uint8_t>(hi | 26));
		fBuf.push_back(static_cast<uint8_t>((arg >> 24) & 0xFF));
		fBuf.push_back(static_cast<uint8_t>((arg >> 16) & 0xFF));
		fBuf.push_back(static_cast<uint8_t>((arg >> 8) & 0xFF));
		fBuf.push_back(static_cast<uint8_t>(arg & 0xFF));
	} else {
		fBuf.push_back(static_cast<uint8_t>(hi | 27));
		for (int shift = 56; shift >= 0; shift -= 8)
			fBuf.push_back(static_cast<uint8_t>((arg >> shift) & 0xFF));
	}
}

void CborWriter::UInt(uint64_t v)
{
	WriteTypeAndArg(0, v);
}

void CborWriter::Int(int64_t v)
{
	if (v < 0) {
		// CBOR negative n encodes argument (-1 - v). In two's complement ~v == -1 - v,
		// which computes the argument without overflow even for INT64_MIN.
		WriteTypeAndArg(1, static_cast<uint64_t>(~v));
	} else {
		WriteTypeAndArg(0, static_cast<uint64_t>(v));
	}
}

void CborWriter::Bytes(const uint8_t* data, size_t length)
{
	WriteTypeAndArg(2, length);
	fBuf.insert(fBuf.end(), data, data + length);
}

void CborWriter::Bytes(const std::vector<uint8_t>& b)
{
	Bytes(b.data(), b.size());
}

void CborWriter::Text(const std::string& s)
{
	WriteTypeAndArg(3, s.size());
	fBuf.insert(fBuf.end(), s.begin(), s.end());
}

void CborWriter::Text(const char* s)
{
	Text(std::string(s));
}

void CborWriter::Bool(bool b)
{
	fBuf.push_back(b ? 0xF5 : 0xF4);
}

void CborWriter::Null()
{
	fBuf.push_back(0xF6);
}

void CborWriter::ArrayHeader(size_t count)
{
	WriteTypeAndArg(4, count);
}

void CborWriter::MapHeader(size_t count)
{
	WriteTypeAndArg(5, count);
}

// ---- Reader ---------------------------------------------------------------------------

bool CborReader::ReadHead(uint8_t& majorType, uint64_t& arg)
{
	if (fError || fPos >= fLen) {
		Fail();
		return false;
	}
	const uint8_t ib = fData[fPos++];
	majorType = static_cast<uint8_t>(ib >> 5);
	const uint8_t ai = ib & 0x1F;

	if (ai < 24) {
		arg = ai;
		return true;
	}

	size_t n;
	switch (ai) {
		case 24: n = 1; break;
		case 25: n = 2; break;
		case 26: n = 4; break;
		case 27: n = 8; break;
		default: // 28, 29, 30 reserved; 31 indefinite: unsupported
			Fail();
			return false;
	}
	if (fLen - fPos < n) {
		Fail();
		return false;
	}
	uint64_t v = 0;
	for (size_t i = 0; i < n; ++i)
		v = (v << 8) | fData[fPos + i];
	fPos += n;
	arg = v;
	return true;
}

CborReader::Type CborReader::Peek() const
{
	if (fError || fPos >= fLen)
		return Type::kInvalid;
	const uint8_t ib = fData[fPos];
	switch (ib >> 5) {
		case 0: return Type::kUInt;
		case 1: return Type::kNegInt;
		case 2: return Type::kBytes;
		case 3: return Type::kText;
		case 4: return Type::kArray;
		case 5: return Type::kMap;
		case 7:
			if (ib == 0xF4 || ib == 0xF5) return Type::kBool;
			if (ib == 0xF6) return Type::kNull;
			return Type::kInvalid; // float, break, other simple values
		default:
			return Type::kInvalid; // major 6 tags unsupported
	}
}

bool CborReader::ReadUInt(uint64_t& out)
{
	uint8_t major;
	uint64_t arg;
	if (!ReadHead(major, arg))
		return false;
	if (major != 0) {
		Fail();
		return false;
	}
	out = arg;
	return true;
}

bool CborReader::ReadInt(int64_t& out)
{
	uint8_t major;
	uint64_t arg;
	if (!ReadHead(major, arg))
		return false;
	if (arg > static_cast<uint64_t>(INT64_MAX)) {
		// For major 0 the value would exceed INT64_MAX; for major 1 the value
		// (-1 - arg) would underflow below INT64_MIN. Either way out of int64 range.
		Fail();
		return false;
	}
	if (major == 0) {
		out = static_cast<int64_t>(arg);
		return true;
	}
	if (major == 1) {
		out = -1 - static_cast<int64_t>(arg);
		return true;
	}
	Fail();
	return false;
}

bool CborReader::ReadBytes(std::vector<uint8_t>& out)
{
	uint8_t major;
	uint64_t arg;
	if (!ReadHead(major, arg))
		return false;
	if (major != 2 || arg > fLen - fPos) {
		Fail();
		return false;
	}
	out.assign(fData + fPos, fData + fPos + arg);
	fPos += arg;
	return true;
}

bool CborReader::ReadText(std::string& out)
{
	uint8_t major;
	uint64_t arg;
	if (!ReadHead(major, arg))
		return false;
	if (major != 3 || arg > fLen - fPos) {
		Fail();
		return false;
	}
	out.assign(reinterpret_cast<const char*>(fData + fPos), static_cast<size_t>(arg));
	fPos += arg;
	return true;
}

bool CborReader::ReadBool(bool& out)
{
	if (fError || fPos >= fLen) {
		Fail();
		return false;
	}
	const uint8_t ib = fData[fPos];
	if (ib == 0xF5) {
		out = true;
		++fPos;
		return true;
	}
	if (ib == 0xF4) {
		out = false;
		++fPos;
		return true;
	}
	Fail();
	return false;
}

bool CborReader::ReadNull()
{
	if (fError || fPos >= fLen || fData[fPos] != 0xF6) {
		Fail();
		return false;
	}
	++fPos;
	return true;
}

bool CborReader::ReadArrayHeader(uint64_t& count)
{
	uint8_t major;
	uint64_t arg;
	if (!ReadHead(major, arg))
		return false;
	// Each element needs at least one byte, so a count beyond the remaining bytes is
	// impossible: reject it (guards caller loops and prevents overflow).
	if (major != 4 || arg > fLen - fPos) {
		Fail();
		return false;
	}
	count = arg;
	return true;
}

bool CborReader::ReadMapHeader(uint64_t& count)
{
	uint8_t major;
	uint64_t arg;
	if (!ReadHead(major, arg))
		return false;
	if (major != 5 || arg > fLen - fPos) {
		Fail();
		return false;
	}
	count = arg;
	return true;
}

bool CborReader::Skip()
{
	// Iterative to bound stack use on hostile nesting. `pending` counts items still owed.
	uint64_t pending = 1;
	while (pending > 0) {
		if (fError || fPos >= fLen) {
			Fail();
			return false;
		}
		const uint8_t ib = fData[fPos];
		if ((ib >> 5) == 7) {
			// Only false/true/null are valid simple values in this subset.
			if (ib == 0xF4 || ib == 0xF5 || ib == 0xF6) {
				++fPos;
				--pending;
				continue;
			}
			Fail();
			return false;
		}

		uint8_t major;
		uint64_t arg;
		if (!ReadHead(major, arg))
			return false;
		--pending;

		switch (major) {
			case 0: // uint
			case 1: // negative
				break;
			case 2: // byte string
			case 3: // text string
				if (arg > fLen - fPos) {
					Fail();
					return false;
				}
				fPos += arg;
				break;
			case 4: // array: `arg` more items
				if (arg > fLen - fPos || arg > UINT64_MAX - pending) {
					Fail();
					return false;
				}
				pending += arg;
				break;
			case 5: // map: `arg` key/value pairs = 2*arg more items
				if (arg > fLen - fPos || arg > (UINT64_MAX - pending) / 2) {
					Fail();
					return false;
				}
				pending += arg * 2;
				break;
			default: // major 6 tags unsupported
				Fail();
				return false;
		}
	}
	return true;
}

} // namespace wire
} // namespace campiello
