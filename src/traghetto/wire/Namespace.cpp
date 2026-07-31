// Namespace.cpp
//
// Implementation of the CNP namespace-mutation messages. See Namespace.h.

#include "Namespace.h"

#include "Cbor.h"
#include "FileOps.h" // EncodeOk / DecodeOk for the Ok replies

namespace campiello {
namespace wire {

std::vector<uint8_t> EncodeMkdirRequest(const std::string& path, uint32_t mode)
{
	CborWriter w;
	// Canonical key order (both length 4, bytewise): mode, path.
	w.MapHeader(2);
	w.Text("mode"); w.UInt(mode);
	w.Text("path"); w.Text(path);
	return w.Take();
}

bool DecodeMkdirRequest(const std::vector<uint8_t>& payload, std::string& path, uint32_t& mode)
{
	CborReader r(payload);
	uint64_t count = 0;
	if (!r.ReadMapHeader(count))
		return false;

	bool haveMode = false, havePath = false;
	uint32_t m = 0;
	std::string p;
	for (uint64_t i = 0; i < count; ++i) {
		std::string key;
		if (!r.ReadText(key))
			return false;
		if (key == "mode") {
			if (haveMode) return false;
			uint64_t v = 0;
			if (!r.ReadUInt(v) || v > 0xFFFFFFFFull) return false;
			m = static_cast<uint32_t>(v);
			haveMode = true;
		} else if (key == "path") {
			if (havePath) return false;
			if (!r.ReadText(p)) return false;
			if (p.empty() || p.size() > kMaxPathBytes) return false;
			havePath = true;
		} else {
			if (!r.Skip()) return false;
		}
	}
	if (r.HasError() || !r.AtEnd() || !haveMode || !havePath)
		return false;
	mode = m;
	path = std::move(p);
	return true;
}

std::vector<uint8_t> EncodeRenameRequest(const std::string& from, const std::string& to)
{
	CborWriter w;
	// Canonical key order (length-first): to (2) before from (4).
	w.MapHeader(2);
	w.Text("to");   w.Text(to);
	w.Text("from"); w.Text(from);
	return w.Take();
}

bool DecodeRenameRequest(const std::vector<uint8_t>& payload, std::string& from, std::string& to)
{
	CborReader r(payload);
	uint64_t count = 0;
	if (!r.ReadMapHeader(count))
		return false;

	bool haveFrom = false, haveTo = false;
	std::string f, t;
	for (uint64_t i = 0; i < count; ++i) {
		std::string key;
		if (!r.ReadText(key))
			return false;
		if (key == "from") {
			if (haveFrom) return false;
			if (!r.ReadText(f)) return false;
			if (f.empty() || f.size() > kMaxPathBytes) return false;
			haveFrom = true;
		} else if (key == "to") {
			if (haveTo) return false;
			if (!r.ReadText(t)) return false;
			if (t.empty() || t.size() > kMaxPathBytes) return false;
			haveTo = true;
		} else {
			if (!r.Skip()) return false;
		}
	}
	if (r.HasError() || !r.AtEnd() || !haveFrom || !haveTo)
		return false;
	from = std::move(f);
	to = std::move(t);
	return true;
}

std::vector<uint8_t> EncodeTruncateRequest(const std::string& path, uint64_t size)
{
	CborWriter w;
	// Canonical key order (both length 4, bytewise): path, size.
	w.MapHeader(2);
	w.Text("path"); w.Text(path);
	w.Text("size"); w.UInt(size);
	return w.Take();
}

bool DecodeTruncateRequest(const std::vector<uint8_t>& payload, std::string& path, uint64_t& size)
{
	CborReader r(payload);
	uint64_t count = 0;
	if (!r.ReadMapHeader(count))
		return false;

	bool havePath = false, haveSize = false;
	std::string p;
	uint64_t s = 0;
	for (uint64_t i = 0; i < count; ++i) {
		std::string key;
		if (!r.ReadText(key))
			return false;
		if (key == "path") {
			if (havePath) return false;
			if (!r.ReadText(p)) return false;
			if (p.empty() || p.size() > kMaxPathBytes) return false;
			havePath = true;
		} else if (key == "size") {
			if (haveSize) return false;
			if (!r.ReadUInt(s)) return false;
			haveSize = true;
		} else {
			if (!r.Skip()) return false;
		}
	}
	if (r.HasError() || !r.AtEnd() || !havePath || !haveSize)
		return false;
	path = std::move(p);
	size = s;
	return true;
}

Frame MakeMkdirRequest(const std::string& path, uint32_t mode, uint32_t requestId)
{
	Frame f;
	f.type = MessageType::kMkdir;
	f.requestId = requestId;
	f.payload = EncodeMkdirRequest(path, mode);
	return f;
}

Frame MakeUnlinkRequest(const std::string& path, uint32_t requestId)
{
	Frame f;
	f.type = MessageType::kUnlink;
	f.requestId = requestId;
	f.payload = EncodePathRequest(path);
	return f;
}

Frame MakeRenameRequest(const std::string& from, const std::string& to, uint32_t requestId)
{
	Frame f;
	f.type = MessageType::kRename;
	f.requestId = requestId;
	f.payload = EncodeRenameRequest(from, to);
	return f;
}

Frame MakeTruncateRequest(const std::string& path, uint64_t size, uint32_t requestId)
{
	Frame f;
	f.type = MessageType::kTruncate;
	f.requestId = requestId;
	f.payload = EncodeTruncateRequest(path, size);
	return f;
}

Frame MakeMkdirReply(uint32_t requestId)
{
	Frame f;
	f.type = MessageType::kMkdir;
	f.requestId = requestId;
	f.payload = EncodeOk();
	return f;
}

Frame MakeUnlinkReply(uint32_t requestId)
{
	Frame f;
	f.type = MessageType::kUnlink;
	f.requestId = requestId;
	f.payload = EncodeOk();
	return f;
}

Frame MakeRenameReply(uint32_t requestId)
{
	Frame f;
	f.type = MessageType::kRename;
	f.requestId = requestId;
	f.payload = EncodeOk();
	return f;
}

Frame MakeTruncateReply(uint32_t requestId)
{
	Frame f;
	f.type = MessageType::kTruncate;
	f.requestId = requestId;
	f.payload = EncodeOk();
	return f;
}

} // namespace wire
} // namespace campiello
