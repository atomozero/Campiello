// Attributes.cpp
//
// Implementation of the CNP AttrSet codec. See Attributes.h.

#include "Attributes.h"

namespace campiello {
namespace wire {

void WriteAttrSet(CborWriter& w, const AttrSet& attrs)
{
	w.ArrayHeader(attrs.size());
	for (const Attr& a : attrs) {
		// Canonical key order (all one byte): n, t, v.
		w.MapHeader(3);
		w.Text("n"); w.Text(a.name);
		w.Text("t"); w.UInt(a.type);
		w.Text("v"); w.Bytes(a.value);
	}
}

// Read one attribute map { n, t, v }.
static bool ReadAttr(CborReader& r, Attr& out)
{
	uint64_t count = 0;
	if (!r.ReadMapHeader(count))
		return false;

	Attr result;
	bool haveName = false;
	bool haveType = false;
	bool haveValue = false;

	for (uint64_t i = 0; i < count; ++i) {
		std::string key;
		if (!r.ReadText(key))
			return false;

		if (key == "n") {
			if (haveName)
				return false;
			if (!r.ReadText(result.name))
				return false;
			if (result.name.empty() || result.name.size() > kMaxAttrNameBytes)
				return false;
			haveName = true;
		} else if (key == "t") {
			if (haveType)
				return false;
			uint64_t t = 0;
			if (!r.ReadUInt(t))
				return false;
			if (t > 0xFFFFFFFFull)
				return false;
			result.type = static_cast<uint32_t>(t);
			haveType = true;
		} else if (key == "v") {
			if (haveValue)
				return false;
			if (!r.ReadBytes(result.value))
				return false;
			haveValue = true;
		} else {
			if (!r.Skip())
				return false;
		}
	}

	if (!haveName || !haveType || !haveValue)
		return false;
	out = std::move(result);
	return true;
}

bool ReadAttrSet(CborReader& r, AttrSet& out)
{
	uint64_t count = 0;
	if (!r.ReadArrayHeader(count))
		return false;
	if (count > kMaxAttrs)
		return false;

	AttrSet result;
	result.reserve(static_cast<size_t>(count));
	for (uint64_t i = 0; i < count; ++i) {
		Attr a;
		if (!ReadAttr(r, a))
			return false;
		result.push_back(std::move(a));
	}
	out = std::move(result);
	return true;
}

} // namespace wire
} // namespace campiello
