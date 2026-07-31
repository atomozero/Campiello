// Listing.cpp
//
// Implementation of the CNP STAT / LIST payloads and the Stat / Entry codecs. See
// Listing.h.

#include "Listing.h"

namespace campiello {
namespace wire {

void WriteStat(CborWriter& w, const Stat& s)
{
	// Canonical key order (length-first then bytewise): m, ct, mt, sz, ino.
	w.MapHeader(5);
	w.Text("m");   w.UInt(s.mode);
	w.Text("ct");  w.Int(s.crtime);
	w.Text("mt");  w.Int(s.mtime);
	w.Text("sz");  w.UInt(s.size);
	w.Text("ino"); w.UInt(s.inode);
}

bool ReadStat(CborReader& r, Stat& out)
{
	uint64_t count = 0;
	if (!r.ReadMapHeader(count))
		return false;

	Stat result;
	bool haveMode = false, haveSize = false, haveMtime = false;
	bool haveCrtime = false, haveInode = false;

	for (uint64_t i = 0; i < count; ++i) {
		std::string key;
		if (!r.ReadText(key))
			return false;

		if (key == "m") {
			if (haveMode) return false;
			uint64_t v = 0;
			if (!r.ReadUInt(v) || v > 0xFFFFFFFFull) return false;
			result.mode = static_cast<uint32_t>(v);
			haveMode = true;
		} else if (key == "sz") {
			if (haveSize) return false;
			if (!r.ReadUInt(result.size)) return false;
			haveSize = true;
		} else if (key == "mt") {
			if (haveMtime) return false;
			if (!r.ReadInt(result.mtime)) return false;
			haveMtime = true;
		} else if (key == "ct") {
			if (haveCrtime) return false;
			if (!r.ReadInt(result.crtime)) return false;
			haveCrtime = true;
		} else if (key == "ino") {
			if (haveInode) return false;
			if (!r.ReadUInt(result.inode)) return false;
			haveInode = true;
		} else {
			if (!r.Skip()) return false;
		}
	}

	if (!haveMode || !haveSize || !haveMtime || !haveCrtime || !haveInode)
		return false;
	out = result;
	return true;
}

void WriteEntry(CborWriter& w, const Entry& e)
{
	// Canonical key order: name, stat, attrs (name < stat bytewise; attrs is longer).
	w.MapHeader(3);
	w.Text("name");  w.Text(e.name);
	w.Text("stat");  WriteStat(w, e.stat);
	w.Text("attrs"); WriteAttrSet(w, e.attrs);
}

bool ReadEntry(CborReader& r, Entry& out)
{
	uint64_t count = 0;
	if (!r.ReadMapHeader(count))
		return false;

	Entry result;
	bool haveName = false, haveStat = false, haveAttrs = false;

	for (uint64_t i = 0; i < count; ++i) {
		std::string key;
		if (!r.ReadText(key))
			return false;

		if (key == "name") {
			if (haveName) return false;
			if (!r.ReadText(result.name)) return false;
			if (result.name.empty() || result.name.size() > kMaxEntryNameBytes)
				return false;
			haveName = true;
		} else if (key == "stat") {
			if (haveStat) return false;
			if (!ReadStat(r, result.stat)) return false;
			haveStat = true;
		} else if (key == "attrs") {
			if (haveAttrs) return false;
			if (!ReadAttrSet(r, result.attrs)) return false;
			haveAttrs = true;
		} else {
			if (!r.Skip()) return false;
		}
	}

	if (!haveName || !haveStat || !haveAttrs)
		return false;
	out = std::move(result);
	return true;
}

std::vector<uint8_t> EncodePathRequest(const std::string& path)
{
	CborWriter w;
	w.MapHeader(1);
	w.Text("path"); w.Text(path);
	return w.Take();
}

bool DecodePathRequest(const std::vector<uint8_t>& payload, std::string& path)
{
	CborReader r(payload);
	uint64_t count = 0;
	if (!r.ReadMapHeader(count))
		return false;

	bool havePath = false;
	std::string result;
	for (uint64_t i = 0; i < count; ++i) {
		std::string key;
		if (!r.ReadText(key))
			return false;
		if (key == "path") {
			if (havePath) return false;
			if (!r.ReadText(result)) return false;
			if (result.empty() || result.size() > kMaxPathBytes) return false;
			havePath = true;
		} else {
			if (!r.Skip()) return false;
		}
	}

	if (r.HasError() || !r.AtEnd() || !havePath)
		return false;
	path = std::move(result);
	return true;
}

std::vector<uint8_t> EncodeStatReply(const Entry& entry)
{
	CborWriter w;
	WriteEntry(w, entry);
	return w.Take();
}

bool DecodeStatReply(const std::vector<uint8_t>& payload, Entry& out)
{
	CborReader r(payload);
	if (!ReadEntry(r, out))
		return false;
	if (r.HasError() || !r.AtEnd())
		return false;
	return true;
}

std::vector<uint8_t> EncodeListing(const std::vector<Entry>& entries)
{
	CborWriter w;
	w.MapHeader(1);
	w.Text("entries");
	w.ArrayHeader(entries.size());
	for (const Entry& e : entries)
		WriteEntry(w, e);
	return w.Take();
}

bool DecodeListing(const std::vector<uint8_t>& payload, std::vector<Entry>& out)
{
	CborReader r(payload);
	uint64_t count = 0;
	if (!r.ReadMapHeader(count))
		return false;

	bool haveEntries = false;
	std::vector<Entry> result;
	for (uint64_t i = 0; i < count; ++i) {
		std::string key;
		if (!r.ReadText(key))
			return false;
		if (key == "entries") {
			if (haveEntries) return false;
			uint64_t n = 0;
			if (!r.ReadArrayHeader(n))
				return false;
			if (n > kMaxEntriesPerReply)
				return false;
			result.reserve(static_cast<size_t>(n));
			for (uint64_t j = 0; j < n; ++j) {
				Entry e;
				if (!ReadEntry(r, e))
					return false;
				result.push_back(std::move(e));
			}
			haveEntries = true;
		} else {
			if (!r.Skip()) return false;
		}
	}

	if (r.HasError() || !r.AtEnd() || !haveEntries)
		return false;
	out = std::move(result);
	return true;
}

Frame MakeStatRequest(const std::string& path, uint32_t requestId)
{
	Frame f;
	f.type = MessageType::kStat;
	f.requestId = requestId;
	f.payload = EncodePathRequest(path);
	return f;
}

Frame MakeStatReply(const Entry& entry, uint32_t requestId)
{
	Frame f;
	f.type = MessageType::kStat;
	f.requestId = requestId;
	f.payload = EncodeStatReply(entry);
	return f;
}

Frame MakeListRequest(const std::string& path, uint32_t requestId)
{
	Frame f;
	f.type = MessageType::kList;
	f.requestId = requestId;
	f.payload = EncodePathRequest(path);
	return f;
}

Frame MakeListReply(const std::vector<Entry>& entries, uint32_t requestId)
{
	Frame f;
	f.type = MessageType::kList;
	f.requestId = requestId;
	f.payload = EncodeListing(entries);
	return f;
}

} // namespace wire
} // namespace campiello
