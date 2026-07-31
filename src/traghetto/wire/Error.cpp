// Error.cpp
//
// Implementation of the CNP ERROR reply. See Error.h.

#include "Error.h"

#include "Cbor.h"

namespace campiello {
namespace wire {

std::vector<uint8_t> EncodeError(ErrorCode code, const std::string& message)
{
	CborWriter w;
	// Canonical key order (length-first): msg (3) before code (4). msg is omitted when
	// empty, giving a single-entry map.
	if (message.empty()) {
		w.MapHeader(1);
		w.Text("code"); w.UInt(static_cast<uint32_t>(code));
	} else {
		w.MapHeader(2);
		w.Text("msg");  w.Text(message);
		w.Text("code"); w.UInt(static_cast<uint32_t>(code));
	}
	return w.Take();
}

bool DecodeError(const std::vector<uint8_t>& payload, ErrorReply& out)
{
	CborReader r(payload);
	uint64_t count = 0;
	if (!r.ReadMapHeader(count))
		return false;

	ErrorReply result;
	bool haveCode = false;
	bool haveMsg = false;

	for (uint64_t i = 0; i < count; ++i) {
		std::string key;
		if (!r.ReadText(key))
			return false;

		if (key == "code") {
			if (haveCode)
				return false;
			uint64_t c = 0;
			if (!r.ReadUInt(c) || c > 0xFFFFFFFFull)
				return false;
			result.code = static_cast<uint32_t>(c);
			haveCode = true;
		} else if (key == "msg") {
			if (haveMsg)
				return false;
			if (!r.ReadText(result.message))
				return false;
			if (result.message.size() > kMaxErrorMessageBytes)
				return false;
			haveMsg = true;
		} else {
			if (!r.Skip())
				return false;
		}
	}

	if (r.HasError() || !r.AtEnd() || !haveCode)
		return false;
	out = std::move(result);
	return true;
}

Frame MakeError(ErrorCode code, const std::string& message, uint32_t requestId)
{
	Frame f;
	f.type = MessageType::kError;
	f.requestId = requestId;
	f.payload = EncodeError(code, message);
	return f;
}

} // namespace wire
} // namespace campiello
