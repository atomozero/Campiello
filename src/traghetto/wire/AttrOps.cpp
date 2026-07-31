// AttrOps.cpp
//
// Implementation of the CNP READ_ATTRS / WRITE_ATTRS messages. See AttrOps.h.

#include "AttrOps.h"

#include "Cbor.h"
#include "FileOps.h" // EncodeOk / DecodeOk for the WRITE_ATTRS reply

namespace campiello {
namespace wire {

std::vector<uint8_t> EncodeReadAttrsReply(const AttrSet& attrs)
{
	CborWriter w;
	w.MapHeader(1);
	w.Text("attrs"); WriteAttrSet(w, attrs);
	return w.Take();
}

bool DecodeReadAttrsReply(const std::vector<uint8_t>& payload, AttrSet& out)
{
	CborReader r(payload);
	uint64_t count = 0;
	if (!r.ReadMapHeader(count))
		return false;

	bool haveAttrs = false;
	AttrSet a;
	for (uint64_t i = 0; i < count; ++i) {
		std::string key;
		if (!r.ReadText(key))
			return false;
		if (key == "attrs") {
			if (haveAttrs) return false;
			if (!ReadAttrSet(r, a)) return false;
			haveAttrs = true;
		} else {
			if (!r.Skip()) return false;
		}
	}
	if (r.HasError() || !r.AtEnd() || !haveAttrs)
		return false;
	out = std::move(a);
	return true;
}

std::vector<uint8_t> EncodeWriteAttrsRequest(const std::string& path, const AttrSet& attrs)
{
	CborWriter w;
	// Canonical key order (length-first): path (4) before attrs (5).
	w.MapHeader(2);
	w.Text("path");  w.Text(path);
	w.Text("attrs"); WriteAttrSet(w, attrs);
	return w.Take();
}

bool DecodeWriteAttrsRequest(const std::vector<uint8_t>& payload, std::string& path,
	AttrSet& attrs)
{
	CborReader r(payload);
	uint64_t count = 0;
	if (!r.ReadMapHeader(count))
		return false;

	bool havePath = false, haveAttrs = false;
	std::string p;
	AttrSet a;
	for (uint64_t i = 0; i < count; ++i) {
		std::string key;
		if (!r.ReadText(key))
			return false;
		if (key == "path") {
			if (havePath) return false;
			if (!r.ReadText(p)) return false;
			if (p.empty() || p.size() > kMaxPathBytes) return false;
			havePath = true;
		} else if (key == "attrs") {
			if (haveAttrs) return false;
			if (!ReadAttrSet(r, a)) return false;
			haveAttrs = true;
		} else {
			if (!r.Skip()) return false;
		}
	}
	if (r.HasError() || !r.AtEnd() || !havePath || !haveAttrs)
		return false;
	path = std::move(p);
	attrs = std::move(a);
	return true;
}

Frame MakeReadAttrsRequest(const std::string& path, uint32_t requestId)
{
	Frame f;
	f.type = MessageType::kReadAttrs;
	f.requestId = requestId;
	f.payload = EncodePathRequest(path);
	return f;
}

Frame MakeReadAttrsReply(const AttrSet& attrs, uint32_t requestId)
{
	Frame f;
	f.type = MessageType::kReadAttrs;
	f.requestId = requestId;
	f.payload = EncodeReadAttrsReply(attrs);
	return f;
}

Frame MakeWriteAttrsRequest(const std::string& path, const AttrSet& attrs, uint32_t requestId)
{
	Frame f;
	f.type = MessageType::kWriteAttrs;
	f.requestId = requestId;
	f.payload = EncodeWriteAttrsRequest(path, attrs);
	return f;
}

Frame MakeWriteAttrsReply(uint32_t requestId)
{
	Frame f;
	f.type = MessageType::kWriteAttrs;
	f.requestId = requestId;
	f.payload = EncodeOk();
	return f;
}

} // namespace wire
} // namespace campiello
