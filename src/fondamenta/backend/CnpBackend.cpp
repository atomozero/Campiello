// CnpBackend.cpp
//
// Implementation of the native-mode client backend. See CnpBackend.h.

#include "CnpBackend.h"

#include "../../traghetto/wire/AttrOps.h"
#include "../../traghetto/wire/Error.h"
#include "../../traghetto/wire/FileOps.h"
#include "../../traghetto/wire/Namespace.h"
#include "../../traghetto/wire/Query.h"

namespace campiello {
namespace fondamenta {

namespace {

BackendStatus FromErrorCode(wire::ErrorCode code)
{
	switch (code) {
		case wire::ErrorCode::kNotFound:       return BackendStatus::kNotFound;
		case wire::ErrorCode::kAccessDenied:   return BackendStatus::kAccessDenied;
		case wire::ErrorCode::kNotADirectory:  return BackendStatus::kNotADirectory;
		case wire::ErrorCode::kIsADirectory:   return BackendStatus::kIsADirectory;
		case wire::ErrorCode::kUnsupported:    return BackendStatus::kUnsupported;
		case wire::ErrorCode::kIOError:        return BackendStatus::kIoError;
		case wire::ErrorCode::kBadHandle:      return BackendStatus::kBadHandle;
		case wire::ErrorCode::kInvalidRequest: return BackendStatus::kInvalidRequest;
		default:                               return BackendStatus::kIoError;
	}
}

// Decode an ERROR frame into a status. A malformed ERROR is a protocol error.
BackendStatus FromErrorFrame(const wire::Frame& frame)
{
	wire::ErrorReply reply;
	if (!wire::DecodeError(frame.payload, reply))
		return BackendStatus::kProtocolError;
	return FromErrorCode((wire::ErrorCode)reply.code);
}

} // namespace

BackendStatus CnpBackend::Hello(const wire::NodeIdentity& self, wire::NodeIdentity& peerOut)
{
	wire::Frame reply;
	if (!fClient.Request(wire::MakeHello(self, 0), reply))
		return BackendStatus::kTransportError;
	if (reply.type == wire::MessageType::kError)
		return FromErrorFrame(reply);
	if (reply.type != wire::MessageType::kWelcome)
		return BackendStatus::kProtocolError;
	if (!wire::DecodeNodeIdentity(reply.payload, peerOut))
		return BackendStatus::kProtocolError;
	return BackendStatus::kOk;
}

BackendStatus CnpBackend::Stat(const std::string& path, wire::Entry& out)
{
	wire::Frame reply;
	if (!fClient.Request(wire::MakeStatRequest(path, 0), reply))
		return BackendStatus::kTransportError;
	if (reply.type == wire::MessageType::kError)
		return FromErrorFrame(reply);
	if (reply.type != wire::MessageType::kStat)
		return BackendStatus::kProtocolError;
	if (!wire::DecodeStatReply(reply.payload, out))
		return BackendStatus::kProtocolError;
	return BackendStatus::kOk;
}

BackendStatus CnpBackend::ReadDir(const std::string& path, std::vector<wire::Entry>& out)
{
	wire::Frame reply;
	if (!fClient.Request(wire::MakeListRequest(path, 0), reply))
		return BackendStatus::kTransportError;
	if (reply.type == wire::MessageType::kError)
		return FromErrorFrame(reply);
	if (reply.type != wire::MessageType::kList)
		return BackendStatus::kProtocolError;
	if (!wire::DecodeListing(reply.payload, out))
		return BackendStatus::kProtocolError;
	return BackendStatus::kOk;
}

BackendStatus CnpBackend::Open(const std::string& path, uint64_t& handle, uint64_t& size)
{
	wire::Frame reply;
	if (!fClient.Request(wire::MakeOpenRequest(path, wire::kOpenRead, 0), reply))
		return BackendStatus::kTransportError;
	if (reply.type == wire::MessageType::kError)
		return FromErrorFrame(reply);
	if (reply.type != wire::MessageType::kOpen)
		return BackendStatus::kProtocolError;
	if (!wire::DecodeOpenReply(reply.payload, handle, size))
		return BackendStatus::kProtocolError;
	return BackendStatus::kOk;
}

BackendStatus CnpBackend::Read(uint64_t handle, uint64_t offset, uint32_t length,
	std::vector<uint8_t>& out)
{
	wire::Frame reply;
	if (!fClient.Request(wire::MakeReadRequest(handle, offset, length, 0), reply))
		return BackendStatus::kTransportError;
	if (reply.type == wire::MessageType::kError)
		return FromErrorFrame(reply);
	if (reply.type != wire::MessageType::kRead)
		return BackendStatus::kProtocolError;
	if (!wire::DecodeReadReply(reply.payload, out))
		return BackendStatus::kProtocolError;
	return BackendStatus::kOk;
}

BackendStatus CnpBackend::Close(uint64_t handle)
{
	return Ack(wire::MakeCloseRequest(handle, 0), wire::MessageType::kClose);
}

BackendStatus CnpBackend::Ack(const wire::Frame& request, wire::MessageType expected)
{
	wire::Frame reply;
	if (!fClient.Request(request, reply))
		return BackendStatus::kTransportError;
	if (reply.type == wire::MessageType::kError)
		return FromErrorFrame(reply);
	if (reply.type != expected)
		return BackendStatus::kProtocolError;
	if (!wire::DecodeOk(reply.payload))
		return BackendStatus::kProtocolError;
	return BackendStatus::kOk;
}

BackendStatus CnpBackend::OpenWrite(const std::string& path, uint64_t& handle)
{
	wire::Frame reply;
	if (!fClient.Request(wire::MakeOpenRequest(path, wire::kOpenWrite, 0), reply))
		return BackendStatus::kTransportError;
	if (reply.type == wire::MessageType::kError)
		return FromErrorFrame(reply);
	if (reply.type != wire::MessageType::kOpen)
		return BackendStatus::kProtocolError;
	uint64_t size = 0;
	if (!wire::DecodeOpenReply(reply.payload, handle, size))
		return BackendStatus::kProtocolError;
	return BackendStatus::kOk;
}

BackendStatus CnpBackend::Write(uint64_t handle, uint64_t offset,
	const std::vector<uint8_t>& data, uint64_t& written)
{
	wire::Frame reply;
	if (!fClient.Request(wire::MakeWriteRequest(handle, offset, data, 0), reply))
		return BackendStatus::kTransportError;
	if (reply.type == wire::MessageType::kError)
		return FromErrorFrame(reply);
	if (reply.type != wire::MessageType::kWrite)
		return BackendStatus::kProtocolError;
	if (!wire::DecodeWriteReply(reply.payload, written))
		return BackendStatus::kProtocolError;
	return BackendStatus::kOk;
}

BackendStatus CnpBackend::Mkdir(const std::string& path, uint32_t mode)
{
	return Ack(wire::MakeMkdirRequest(path, mode, 0), wire::MessageType::kMkdir);
}

BackendStatus CnpBackend::Unlink(const std::string& path)
{
	return Ack(wire::MakeUnlinkRequest(path, 0), wire::MessageType::kUnlink);
}

BackendStatus CnpBackend::Rename(const std::string& from, const std::string& to)
{
	return Ack(wire::MakeRenameRequest(from, to, 0), wire::MessageType::kRename);
}

BackendStatus CnpBackend::Truncate(const std::string& path, uint64_t size)
{
	return Ack(wire::MakeTruncateRequest(path, size, 0), wire::MessageType::kTruncate);
}

BackendStatus CnpBackend::ReadAttrs(const std::string& path, wire::AttrSet& out)
{
	wire::Frame reply;
	if (!fClient.Request(wire::MakeReadAttrsRequest(path, 0), reply))
		return BackendStatus::kTransportError;
	if (reply.type == wire::MessageType::kError)
		return FromErrorFrame(reply);
	if (reply.type != wire::MessageType::kReadAttrs)
		return BackendStatus::kProtocolError;
	if (!wire::DecodeReadAttrsReply(reply.payload, out))
		return BackendStatus::kProtocolError;
	return BackendStatus::kOk;
}

BackendStatus CnpBackend::WriteAttrs(const std::string& path, const wire::AttrSet& attrs)
{
	return Ack(wire::MakeWriteAttrsRequest(path, attrs, 0), wire::MessageType::kWriteAttrs);
}

BackendStatus CnpBackend::Query(const std::string& predicate, std::vector<wire::Entry>& out)
{
	const uint64_t queryId = fNextQueryId++;
	wire::Frame reply;
	if (!fClient.Request(wire::MakeQueryOpenRequest(queryId, predicate, 0), reply))
		return BackendStatus::kTransportError;
	if (reply.type == wire::MessageType::kError)
		return FromErrorFrame(reply);
	if (reply.type != wire::MessageType::kQueryResult)
		return BackendStatus::kProtocolError;

	uint64_t replyId = 0;
	bool done = false;
	if (!wire::DecodeQueryResultReply(reply.payload, replyId, out, done) || replyId != queryId)
		return BackendStatus::kProtocolError;
	// Initial result set only: the peer sends one QUERY_RESULT (done=true). Batched results
	// (done=false with more frames) and live QUERY_UPDATE streaming are a follow-up that needs a
	// persistent receive loop rather than one request/reply.

	// Tell the peer we are done with the query (a no-op Ok on an initial-only responder, correct
	// hygiene once it keeps a live session). Best-effort: the results are already in hand.
	wire::Frame closeReply;
	(void)fClient.Request(wire::MakeQueryCloseRequest(queryId, 0), closeReply);
	return BackendStatus::kOk;
}

} // namespace fondamenta
} // namespace campiello
