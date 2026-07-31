// FileOps.cpp
//
// Implementation of the CNP OPEN / READ / CLOSE messages. See FileOps.h.

#include "FileOps.h"

#include "Cbor.h"

namespace campiello {
namespace wire {

std::vector<uint8_t> EncodeOpenRequest(const std::string& path, uint32_t mode)
{
	CborWriter w;
	// Canonical key order (both length 4, bytewise): mode, path.
	w.MapHeader(2);
	w.Text("mode"); w.UInt(mode);
	w.Text("path"); w.Text(path);
	return w.Take();
}

bool DecodeOpenRequest(const std::vector<uint8_t>& payload, std::string& path, uint32_t& mode)
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

std::vector<uint8_t> EncodeOpenReply(uint64_t handle, uint64_t size)
{
	CborWriter w;
	// Canonical key order (length-first): size (4) before handle (6).
	w.MapHeader(2);
	w.Text("size");   w.UInt(size);
	w.Text("handle"); w.UInt(handle);
	return w.Take();
}

bool DecodeOpenReply(const std::vector<uint8_t>& payload, uint64_t& handle, uint64_t& size)
{
	CborReader r(payload);
	uint64_t count = 0;
	if (!r.ReadMapHeader(count))
		return false;

	bool haveHandle = false, haveSize = false;
	uint64_t h = 0, s = 0;
	for (uint64_t i = 0; i < count; ++i) {
		std::string key;
		if (!r.ReadText(key))
			return false;
		if (key == "handle") {
			if (haveHandle) return false;
			if (!r.ReadUInt(h)) return false;
			haveHandle = true;
		} else if (key == "size") {
			if (haveSize) return false;
			if (!r.ReadUInt(s)) return false;
			haveSize = true;
		} else {
			if (!r.Skip()) return false;
		}
	}
	if (r.HasError() || !r.AtEnd() || !haveHandle || !haveSize)
		return false;
	handle = h;
	size = s;
	return true;
}

std::vector<uint8_t> EncodeReadRequest(uint64_t handle, uint64_t offset, uint32_t length)
{
	CborWriter w;
	// Canonical key order (all length 6, bytewise): handle, length, offset.
	w.MapHeader(3);
	w.Text("handle"); w.UInt(handle);
	w.Text("length"); w.UInt(length);
	w.Text("offset"); w.UInt(offset);
	return w.Take();
}

bool DecodeReadRequest(const std::vector<uint8_t>& payload, uint64_t& handle,
	uint64_t& offset, uint32_t& length)
{
	CborReader r(payload);
	uint64_t count = 0;
	if (!r.ReadMapHeader(count))
		return false;

	bool haveHandle = false, haveOffset = false, haveLength = false;
	uint64_t h = 0, off = 0;
	uint32_t len = 0;
	for (uint64_t i = 0; i < count; ++i) {
		std::string key;
		if (!r.ReadText(key))
			return false;
		if (key == "handle") {
			if (haveHandle) return false;
			if (!r.ReadUInt(h)) return false;
			haveHandle = true;
		} else if (key == "offset") {
			if (haveOffset) return false;
			if (!r.ReadUInt(off)) return false;
			haveOffset = true;
		} else if (key == "length") {
			if (haveLength) return false;
			uint64_t v = 0;
			if (!r.ReadUInt(v) || v > kMaxReadLength) return false;
			len = static_cast<uint32_t>(v);
			haveLength = true;
		} else {
			if (!r.Skip()) return false;
		}
	}
	if (r.HasError() || !r.AtEnd() || !haveHandle || !haveOffset || !haveLength)
		return false;
	handle = h;
	offset = off;
	length = len;
	return true;
}

std::vector<uint8_t> EncodeReadReply(const std::vector<uint8_t>& data)
{
	CborWriter w;
	w.MapHeader(1);
	w.Text("data"); w.Bytes(data);
	return w.Take();
}

bool DecodeReadReply(const std::vector<uint8_t>& payload, std::vector<uint8_t>& data)
{
	CborReader r(payload);
	uint64_t count = 0;
	if (!r.ReadMapHeader(count))
		return false;

	bool haveData = false;
	std::vector<uint8_t> d;
	for (uint64_t i = 0; i < count; ++i) {
		std::string key;
		if (!r.ReadText(key))
			return false;
		if (key == "data") {
			if (haveData) return false;
			if (!r.ReadBytes(d)) return false;
			if (d.size() > kMaxReadLength) return false;
			haveData = true;
		} else {
			if (!r.Skip()) return false;
		}
	}
	if (r.HasError() || !r.AtEnd() || !haveData)
		return false;
	data = std::move(d);
	return true;
}

std::vector<uint8_t> EncodeWriteRequest(uint64_t handle, uint64_t offset,
	const std::vector<uint8_t>& data)
{
	CborWriter w;
	// Canonical key order (length-first): data (4) before handle (6) before offset (6).
	w.MapHeader(3);
	w.Text("data");   w.Bytes(data);
	w.Text("handle"); w.UInt(handle);
	w.Text("offset"); w.UInt(offset);
	return w.Take();
}

bool DecodeWriteRequest(const std::vector<uint8_t>& payload, uint64_t& handle,
	uint64_t& offset, std::vector<uint8_t>& data)
{
	CborReader r(payload);
	uint64_t count = 0;
	if (!r.ReadMapHeader(count))
		return false;

	bool haveHandle = false, haveOffset = false, haveData = false;
	uint64_t h = 0, off = 0;
	std::vector<uint8_t> d;
	for (uint64_t i = 0; i < count; ++i) {
		std::string key;
		if (!r.ReadText(key))
			return false;
		if (key == "handle") {
			if (haveHandle) return false;
			if (!r.ReadUInt(h)) return false;
			haveHandle = true;
		} else if (key == "offset") {
			if (haveOffset) return false;
			if (!r.ReadUInt(off)) return false;
			haveOffset = true;
		} else if (key == "data") {
			if (haveData) return false;
			if (!r.ReadBytes(d)) return false;
			if (d.size() > kMaxWriteLength) return false;
			haveData = true;
		} else {
			if (!r.Skip()) return false;
		}
	}
	if (r.HasError() || !r.AtEnd() || !haveHandle || !haveOffset || !haveData)
		return false;
	handle = h;
	offset = off;
	data = std::move(d);
	return true;
}

std::vector<uint8_t> EncodeWriteReply(uint64_t written)
{
	CborWriter w;
	w.MapHeader(1);
	w.Text("written"); w.UInt(written);
	return w.Take();
}

bool DecodeWriteReply(const std::vector<uint8_t>& payload, uint64_t& written)
{
	CborReader r(payload);
	uint64_t count = 0;
	if (!r.ReadMapHeader(count))
		return false;

	bool haveWritten = false;
	uint64_t n = 0;
	for (uint64_t i = 0; i < count; ++i) {
		std::string key;
		if (!r.ReadText(key))
			return false;
		if (key == "written") {
			if (haveWritten) return false;
			if (!r.ReadUInt(n)) return false;
			haveWritten = true;
		} else {
			if (!r.Skip()) return false;
		}
	}
	if (r.HasError() || !r.AtEnd() || !haveWritten)
		return false;
	written = n;
	return true;
}

std::vector<uint8_t> EncodeCloseRequest(uint64_t handle)
{
	CborWriter w;
	w.MapHeader(1);
	w.Text("handle"); w.UInt(handle);
	return w.Take();
}

bool DecodeCloseRequest(const std::vector<uint8_t>& payload, uint64_t& handle)
{
	CborReader r(payload);
	uint64_t count = 0;
	if (!r.ReadMapHeader(count))
		return false;

	bool haveHandle = false;
	uint64_t h = 0;
	for (uint64_t i = 0; i < count; ++i) {
		std::string key;
		if (!r.ReadText(key))
			return false;
		if (key == "handle") {
			if (haveHandle) return false;
			if (!r.ReadUInt(h)) return false;
			haveHandle = true;
		} else {
			if (!r.Skip()) return false;
		}
	}
	if (r.HasError() || !r.AtEnd() || !haveHandle)
		return false;
	handle = h;
	return true;
}

std::vector<uint8_t> EncodeOk()
{
	CborWriter w;
	w.MapHeader(0);
	return w.Take();
}

bool DecodeOk(const std::vector<uint8_t>& payload)
{
	CborReader r(payload);
	uint64_t count = 0;
	if (!r.ReadMapHeader(count))
		return false;
	// Tolerate future fields: skip a key and its value for each entry.
	for (uint64_t i = 0; i < count; ++i) {
		std::string key;
		if (!r.ReadText(key))
			return false;
		if (!r.Skip())
			return false;
	}
	return !r.HasError() && r.AtEnd();
}

Frame MakeOpenRequest(const std::string& path, uint32_t mode, uint32_t requestId)
{
	Frame f;
	f.type = MessageType::kOpen;
	f.requestId = requestId;
	f.payload = EncodeOpenRequest(path, mode);
	return f;
}

Frame MakeOpenReply(uint64_t handle, uint64_t size, uint32_t requestId)
{
	Frame f;
	f.type = MessageType::kOpen;
	f.requestId = requestId;
	f.payload = EncodeOpenReply(handle, size);
	return f;
}

Frame MakeReadRequest(uint64_t handle, uint64_t offset, uint32_t length, uint32_t requestId)
{
	Frame f;
	f.type = MessageType::kRead;
	f.requestId = requestId;
	f.payload = EncodeReadRequest(handle, offset, length);
	return f;
}

Frame MakeReadReply(const std::vector<uint8_t>& data, uint32_t requestId)
{
	Frame f;
	f.type = MessageType::kRead;
	f.requestId = requestId;
	f.payload = EncodeReadReply(data);
	return f;
}

Frame MakeWriteRequest(uint64_t handle, uint64_t offset, const std::vector<uint8_t>& data,
	uint32_t requestId)
{
	Frame f;
	f.type = MessageType::kWrite;
	f.requestId = requestId;
	f.payload = EncodeWriteRequest(handle, offset, data);
	return f;
}

Frame MakeWriteReply(uint64_t written, uint32_t requestId)
{
	Frame f;
	f.type = MessageType::kWrite;
	f.requestId = requestId;
	f.payload = EncodeWriteReply(written);
	return f;
}

Frame MakeCloseRequest(uint64_t handle, uint32_t requestId)
{
	Frame f;
	f.type = MessageType::kClose;
	f.requestId = requestId;
	f.payload = EncodeCloseRequest(handle);
	return f;
}

Frame MakeCloseReply(uint32_t requestId)
{
	Frame f;
	f.type = MessageType::kClose;
	f.requestId = requestId;
	f.payload = EncodeOk();
	return f;
}

} // namespace wire
} // namespace campiello
