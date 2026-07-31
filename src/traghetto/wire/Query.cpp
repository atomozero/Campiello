// Query.cpp - see Query.h. CBOR keys are in canonical order (length-first, then lexicographic).

#include "Query.h"

#include "Cbor.h"

namespace campiello {
namespace wire {

// ----- QUERY_OPEN { query, queryId } ----------------------------------------------------------

std::vector<uint8_t> EncodeQueryOpenRequest(uint64_t queryId, const std::string& query)
{
	CborWriter w;
	w.MapHeader(2);
	w.Text("query");   w.Text(query);
	w.Text("queryId"); w.UInt(queryId);
	return w.Take();
}

bool DecodeQueryOpenRequest(const std::vector<uint8_t>& payload, uint64_t& queryId,
	std::string& query)
{
	CborReader r(payload);
	uint64_t count = 0;
	if (!r.ReadMapHeader(count))
		return false;
	bool haveQuery = false, haveId = false;
	for (uint64_t i = 0; i < count; ++i) {
		std::string key;
		if (!r.ReadText(key))
			return false;
		if (key == "query") {
			if (haveQuery || !r.ReadText(query) || query.size() > kMaxQueryBytes)
				return false;
			haveQuery = true;
		} else if (key == "queryId") {
			if (haveId || !r.ReadUInt(queryId))
				return false;
			haveId = true;
		} else if (!r.Skip()) {
			return false;
		}
	}
	return !r.HasError() && r.AtEnd() && haveQuery && haveId;
}

// ----- QUERY_RESULT { done, entries, queryId } ------------------------------------------------

std::vector<uint8_t> EncodeQueryResultReply(uint64_t queryId, const std::vector<Entry>& entries,
	bool done)
{
	CborWriter w;
	w.MapHeader(3);
	w.Text("done");    w.Bool(done);
	w.Text("entries"); w.ArrayHeader(entries.size());
	for (const Entry& e : entries)
		WriteEntry(w, e);
	w.Text("queryId"); w.UInt(queryId);
	return w.Take();
}

bool DecodeQueryResultReply(const std::vector<uint8_t>& payload, uint64_t& queryId,
	std::vector<Entry>& entries, bool& done)
{
	CborReader r(payload);
	uint64_t count = 0;
	if (!r.ReadMapHeader(count))
		return false;
	bool haveDone = false, haveEntries = false, haveId = false;
	entries.clear();
	for (uint64_t i = 0; i < count; ++i) {
		std::string key;
		if (!r.ReadText(key))
			return false;
		if (key == "done") {
			if (haveDone || !r.ReadBool(done))
				return false;
			haveDone = true;
		} else if (key == "entries") {
			if (haveEntries)
				return false;
			uint64_t n = 0;
			if (!r.ReadArrayHeader(n) || n > kMaxEntriesPerReply)
				return false;
			entries.reserve(static_cast<size_t>(n));
			for (uint64_t j = 0; j < n; ++j) {
				Entry e;
				if (!ReadEntry(r, e))
					return false;
				entries.push_back(std::move(e));
			}
			haveEntries = true;
		} else if (key == "queryId") {
			if (haveId || !r.ReadUInt(queryId))
				return false;
			haveId = true;
		} else if (!r.Skip()) {
			return false;
		}
	}
	return !r.HasError() && r.AtEnd() && haveDone && haveEntries && haveId;
}

// ----- QUERY_UPDATE { added, entry, queryId } -------------------------------------------------

std::vector<uint8_t> EncodeQueryUpdate(uint64_t queryId, bool added, const Entry& entry)
{
	CborWriter w;
	w.MapHeader(3);
	w.Text("added");   w.Bool(added);
	w.Text("entry");   WriteEntry(w, entry);
	w.Text("queryId"); w.UInt(queryId);
	return w.Take();
}

bool DecodeQueryUpdate(const std::vector<uint8_t>& payload, uint64_t& queryId, bool& added,
	Entry& entry)
{
	CborReader r(payload);
	uint64_t count = 0;
	if (!r.ReadMapHeader(count))
		return false;
	bool haveAdded = false, haveEntry = false, haveId = false;
	for (uint64_t i = 0; i < count; ++i) {
		std::string key;
		if (!r.ReadText(key))
			return false;
		if (key == "added") {
			if (haveAdded || !r.ReadBool(added))
				return false;
			haveAdded = true;
		} else if (key == "entry") {
			if (haveEntry || !ReadEntry(r, entry))
				return false;
			haveEntry = true;
		} else if (key == "queryId") {
			if (haveId || !r.ReadUInt(queryId))
				return false;
			haveId = true;
		} else if (!r.Skip()) {
			return false;
		}
	}
	return !r.HasError() && r.AtEnd() && haveAdded && haveEntry && haveId;
}

// ----- QUERY_CLOSE { queryId } ----------------------------------------------------------------

std::vector<uint8_t> EncodeQueryCloseRequest(uint64_t queryId)
{
	CborWriter w;
	w.MapHeader(1);
	w.Text("queryId"); w.UInt(queryId);
	return w.Take();
}

bool DecodeQueryCloseRequest(const std::vector<uint8_t>& payload, uint64_t& queryId)
{
	CborReader r(payload);
	uint64_t count = 0;
	if (!r.ReadMapHeader(count))
		return false;
	bool haveId = false;
	for (uint64_t i = 0; i < count; ++i) {
		std::string key;
		if (!r.ReadText(key))
			return false;
		if (key == "queryId") {
			if (haveId || !r.ReadUInt(queryId))
				return false;
			haveId = true;
		} else if (!r.Skip()) {
			return false;
		}
	}
	return !r.HasError() && r.AtEnd() && haveId;
}

// ----- Frame builders -------------------------------------------------------------------------

Frame MakeQueryOpenRequest(uint64_t queryId, const std::string& query, uint32_t requestId)
{
	Frame f;
	f.type = MessageType::kQueryOpen;
	f.requestId = requestId;
	f.payload = EncodeQueryOpenRequest(queryId, query);
	return f;
}

Frame MakeQueryResultReply(uint64_t queryId, const std::vector<Entry>& entries, bool done,
	uint32_t requestId)
{
	Frame f;
	f.type = MessageType::kQueryResult;
	f.requestId = requestId;
	f.payload = EncodeQueryResultReply(queryId, entries, done);
	return f;
}

Frame MakeQueryUpdate(uint64_t queryId, bool added, const Entry& entry, uint32_t requestId)
{
	Frame f;
	f.type = MessageType::kQueryUpdate;
	f.requestId = requestId;
	f.payload = EncodeQueryUpdate(queryId, added, entry);
	return f;
}

Frame MakeQueryCloseRequest(uint64_t queryId, uint32_t requestId)
{
	Frame f;
	f.type = MessageType::kQueryClose;
	f.requestId = requestId;
	f.payload = EncodeQueryCloseRequest(queryId);
	return f;
}

Frame MakeQueryCloseReply(uint32_t requestId)
{
	Frame f;
	f.type = MessageType::kQueryClose;
	f.requestId = requestId;
	CborWriter w;
	w.MapHeader(0); // Ok ack
	f.payload = w.Take();
	return f;
}

} // namespace wire
} // namespace campiello
