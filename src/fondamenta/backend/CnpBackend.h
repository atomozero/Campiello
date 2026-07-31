// CnpBackend.h
//
// The native (Campiello Native Protocol) backend: implements PeerBackend by translating
// each call into a CNP request over a FrameChannel and decoding the reply. This is the
// client half of native mode; the server half is server/FileServer. It drives the full
// surface, read and write (whether a write succeeds is the peer's decision).
//
// Portable (built on the traghetto wire/dispatch layers), so it and its loopback test build
// in CI. Fondamenta owns one CnpBackend per connected peer.

#ifndef CAMPIELLO_FONDAMENTA_BACKEND_CNPBACKEND_H
#define CAMPIELLO_FONDAMENTA_BACKEND_CNPBACKEND_H

#include "PeerBackend.h"

#include "../../traghetto/dispatch/Dispatch.h"
#include "../../traghetto/transport/FrameChannel.h"
#include "../../traghetto/wire/Handshake.h"

namespace campiello {
namespace fondamenta {

class CnpBackend : public PeerBackend {
public:
	// The channel (a TlsConnection in native mode) must outlive this backend.
	explicit CnpBackend(net::FrameChannel& channel) : fClient(channel) {}

	// Perform the HELLO/WELCOME handshake: announce `self`, receive the peer's identity.
	// Trust (fingerprint pinning) is handled at the TLS layer; this is the CNP-level
	// capability/identity exchange.
	BackendStatus Hello(const wire::NodeIdentity& self, wire::NodeIdentity& peerOut);

	BackendStatus Stat(const std::string& path, wire::Entry& out) override;
	BackendStatus ReadDir(const std::string& path, std::vector<wire::Entry>& out) override;
	BackendStatus Open(const std::string& path, uint64_t& handle, uint64_t& size) override;
	BackendStatus Read(uint64_t handle, uint64_t offset, uint32_t length,
		std::vector<uint8_t>& out) override;
	BackendStatus Close(uint64_t handle) override;

	BackendStatus OpenWrite(const std::string& path, uint64_t& handle) override;
	BackendStatus Write(uint64_t handle, uint64_t offset, const std::vector<uint8_t>& data,
		uint64_t& written) override;
	BackendStatus Mkdir(const std::string& path, uint32_t mode) override;
	BackendStatus Unlink(const std::string& path) override;
	BackendStatus Rename(const std::string& from, const std::string& to) override;
	BackendStatus Truncate(const std::string& path, uint64_t size) override;
	BackendStatus ReadAttrs(const std::string& path, wire::AttrSet& out) override;
	BackendStatus WriteAttrs(const std::string& path, const wire::AttrSet& attrs) override;
	BackendStatus Query(const std::string& predicate, std::vector<wire::Entry>& out) override;

private:
	// Send a request whose success reply is an Ok ack of type `expected`, returning the mapped
	// status. Used by the mutations (MKDIR/UNLINK/RENAME/TRUNCATE/WRITE_ATTRS).
	BackendStatus Ack(const wire::Frame& request, wire::MessageType expected);

	net::Client fClient;
	uint64_t fNextQueryId = 1; // client-assigned query handles
};

} // namespace fondamenta
} // namespace campiello

#endif // CAMPIELLO_FONDAMENTA_BACKEND_CNPBACKEND_H
