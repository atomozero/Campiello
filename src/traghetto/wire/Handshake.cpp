// Handshake.cpp
//
// Implementation of the HELLO / WELCOME handshake payloads. See Handshake.h.

#include "Handshake.h"

#include "Cbor.h"

namespace campiello {
namespace wire {

bool NodeIdentity::HasCap(const std::string& cap) const
{
	for (const std::string& c : caps)
		if (c == cap)
			return true;
	return false;
}

std::vector<uint8_t> EncodeNodeIdentity(const NodeIdentity& id)
{
	CborWriter w;
	// Canonical CBOR map key order is length-first then bytewise: v, fp, caps, node.
	w.MapHeader(4);
	w.Text("v");    w.UInt(id.version);
	w.Text("fp");   w.Bytes(id.fingerprint);
	w.Text("caps"); w.ArrayHeader(id.caps.size());
	for (const std::string& c : id.caps)
		w.Text(c);
	w.Text("node"); w.Text(id.node);
	return w.Take();
}

bool DecodeNodeIdentity(const uint8_t* data, size_t length, NodeIdentity& out)
{
	CborReader r(data, length);
	uint64_t count = 0;
	if (!r.ReadMapHeader(count))
		return false;

	NodeIdentity result;
	bool haveVersion = false;
	bool haveFingerprint = false;
	bool haveNode = false;
	// caps is optional; absence means an empty list.

	for (uint64_t i = 0; i < count; ++i) {
		std::string key;
		if (!r.ReadText(key))
			return false;

		if (key == "v") {
			if (haveVersion)
				return false; // duplicate key
			uint64_t v = 0;
			if (!r.ReadUInt(v))
				return false;
			if (v == 0 || v > 0xFFFFFFFFull)
				return false;
			result.version = static_cast<uint32_t>(v);
			haveVersion = true;
		} else if (key == "fp") {
			if (haveFingerprint)
				return false;
			if (!r.ReadBytes(result.fingerprint))
				return false;
			if (result.fingerprint.size() != kFingerprintBytes)
				return false;
			haveFingerprint = true;
		} else if (key == "node") {
			if (haveNode)
				return false;
			if (!r.ReadText(result.node))
				return false;
			if (result.node.size() > kMaxNodeNameBytes)
				return false;
			haveNode = true;
		} else if (key == "caps") {
			if (!result.caps.empty())
				return false; // duplicate key
			uint64_t n = 0;
			if (!r.ReadArrayHeader(n))
				return false;
			if (n > kMaxCaps)
				return false;
			result.caps.reserve(static_cast<size_t>(n));
			for (uint64_t j = 0; j < n; ++j) {
				std::string cap;
				if (!r.ReadText(cap))
					return false;
				if (cap.size() > kMaxCapBytes)
					return false;
				result.caps.push_back(std::move(cap));
			}
		} else {
			// Unknown key: skip its value for forward compatibility.
			if (!r.Skip())
				return false;
		}
	}

	if (r.HasError() || !r.AtEnd())
		return false;
	if (!haveVersion || !haveFingerprint || !haveNode)
		return false;

	out = std::move(result);
	return true;
}

bool DecodeNodeIdentity(const std::vector<uint8_t>& payload, NodeIdentity& out)
{
	return DecodeNodeIdentity(payload.data(), payload.size(), out);
}

Frame MakeHello(const NodeIdentity& id, uint32_t requestId)
{
	Frame f;
	f.type = MessageType::kHello;
	f.requestId = requestId;
	f.payload = EncodeNodeIdentity(id);
	return f;
}

Frame MakeWelcome(const NodeIdentity& id, uint32_t requestId)
{
	Frame f;
	f.type = MessageType::kWelcome;
	f.requestId = requestId;
	f.payload = EncodeNodeIdentity(id);
	return f;
}

} // namespace wire
} // namespace campiello
