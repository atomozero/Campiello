# Build and run the server tests (multi-peer daemon + file server).
# OpenSSL + POSIX sockets + std::thread. Portable: OpenSSL is on Haiku and Linux.

CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -Werror -O2 -pthread
LDLIBS ?= -lssl -lcrypto
ifeq ($(shell uname),Haiku)
LDLIBS += -lnetwork
endif

WIRE_DIR := ../../src/traghetto/wire
NET_DIR := ../../src/traghetto/transport
TLS_DIR := ../../src/traghetto/tls
DISP_DIR := ../../src/traghetto/dispatch
SRV_DIR := ../../src/traghetto/server
TRUST_DIR := ../../src/traghetto/trust
MDNS_DIR := ../../src/bricola/mdns

# Bricola (discovery), linked into the resident daemon: DaemonApp advertises and browses.
BRICOLA_SRCS := $(MDNS_DIR)/Bricola.cpp $(MDNS_DIR)/Responder.cpp $(MDNS_DIR)/Browser.cpp \
                $(MDNS_DIR)/PeerTable.cpp $(MDNS_DIR)/MdnsSocket.cpp $(MDNS_DIR)/MdnsWire.cpp

# Transport + protocol sources shared by both tests.
COMMON := $(SRV_DIR)/Daemon.cpp $(DISP_DIR)/Dispatch.cpp $(DISP_DIR)/FrameWriter.cpp $(NET_DIR)/Connection.cpp \
          $(TLS_DIR)/TlsConnection.cpp $(TLS_DIR)/Identity.cpp \
          $(WIRE_DIR)/FrameCodec.cpp $(WIRE_DIR)/Cbor.cpp $(WIRE_DIR)/Handshake.cpp \
          $(WIRE_DIR)/Error.cpp

TESTS := test_daemon test_fileserver test_gatedhandler test_servernode test_filewrite

test_daemon: test_daemon.cpp $(COMMON)
	$(CXX) $(CXXFLAGS) -o $@ test_daemon.cpp $(COMMON) $(LDLIBS)

# The pairing gate needs no OpenSSL or sockets: it is exercised frame by frame.
GATE_SRCS := $(SRV_DIR)/GatedHandler.cpp $(TRUST_DIR)/Pairing.cpp \
             $(TRUST_DIR)/TokenBucket.cpp $(TRUST_DIR)/TrustStore.cpp \
             $(TLS_DIR)/Fingerprint.cpp \
             $(WIRE_DIR)/Handshake.cpp $(WIRE_DIR)/Error.cpp \
             $(WIRE_DIR)/FrameCodec.cpp $(WIRE_DIR)/Cbor.cpp

test_gatedhandler: test_gatedhandler.cpp $(GATE_SRCS)
	$(CXX) $(CXXFLAGS) -o $@ test_gatedhandler.cpp $(GATE_SRCS)

# The full server-side stack behind ServerNode (no BApplication, no UI).
NODE_STACK := $(SRV_DIR)/ServerNode.cpp $(SRV_DIR)/GatedHandler.cpp \
              $(SRV_DIR)/FileServer.cpp $(SRV_DIR)/Daemon.cpp $(DISP_DIR)/Dispatch.cpp $(DISP_DIR)/FrameWriter.cpp \
              $(NET_DIR)/Connection.cpp $(TLS_DIR)/TlsConnection.cpp $(TLS_DIR)/Identity.cpp \
              $(TLS_DIR)/Fingerprint.cpp $(TRUST_DIR)/Pairing.cpp $(TRUST_DIR)/TokenBucket.cpp \
              $(TRUST_DIR)/TrustStore.cpp $(TRUST_DIR)/Paths.cpp \
              $(WIRE_DIR)/FrameCodec.cpp $(WIRE_DIR)/Cbor.cpp $(WIRE_DIR)/Handshake.cpp \
              $(WIRE_DIR)/Error.cpp $(WIRE_DIR)/Listing.cpp $(WIRE_DIR)/Attributes.cpp \
              $(WIRE_DIR)/FileOps.cpp $(WIRE_DIR)/Namespace.cpp $(WIRE_DIR)/AttrOps.cpp \
              $(WIRE_DIR)/Query.cpp

# FileServer runs a Haiku BQuery for the QUERY path, so ServerNode-based tests link libbe.
test_servernode: test_servernode.cpp $(NODE_STACK) ../../src/fondamenta/backend/CnpBackend.cpp
	$(CXX) $(CXXFLAGS) -o $@ $^ -lbe $(LDLIBS)

# FileServer now runs Haiku BQuery for the QUERY path, so it needs libbe and the Query wire codec.
test_fileserver: test_fileserver.cpp $(SRV_DIR)/FileServer.cpp $(COMMON) \
                 $(WIRE_DIR)/Listing.cpp $(WIRE_DIR)/Attributes.cpp $(WIRE_DIR)/FileOps.cpp \
                 $(WIRE_DIR)/Namespace.cpp $(WIRE_DIR)/AttrOps.cpp $(WIRE_DIR)/Query.cpp
	$(CXX) $(CXXFLAGS) -o $@ test_fileserver.cpp $(SRV_DIR)/FileServer.cpp $(COMMON) \
	    $(WIRE_DIR)/Listing.cpp $(WIRE_DIR)/Attributes.cpp $(WIRE_DIR)/FileOps.cpp \
	    $(WIRE_DIR)/Namespace.cpp $(WIRE_DIR)/AttrOps.cpp $(WIRE_DIR)/Query.cpp -lbe $(LDLIBS)

# The write path exercised directly through FileServer::Handle (no socket, no TLS), so it
# links only the wire codecs and the handler, and needs no OpenSSL.
FILEWRITE_SRCS := $(SRV_DIR)/FileServer.cpp $(DISP_DIR)/Dispatch.cpp $(DISP_DIR)/FrameWriter.cpp $(NET_DIR)/Connection.cpp \
                  $(WIRE_DIR)/Listing.cpp $(WIRE_DIR)/Attributes.cpp $(WIRE_DIR)/FileOps.cpp \
                  $(WIRE_DIR)/Namespace.cpp $(WIRE_DIR)/AttrOps.cpp $(WIRE_DIR)/Query.cpp \
                  $(WIRE_DIR)/Error.cpp \
                  $(WIRE_DIR)/Handshake.cpp $(WIRE_DIR)/FrameCodec.cpp $(WIRE_DIR)/Cbor.cpp

test_filewrite: test_filewrite.cpp $(FILEWRITE_SRCS)
	$(CXX) $(CXXFLAGS) -o $@ test_filewrite.cpp $(FILEWRITE_SRCS) -lbe $(LDLIBS)

.PHONY: check clean daemon-build
check: $(TESTS) daemon-build
	@for t in $(TESTS); do echo "== $$t =="; ./$$t || exit 1; done

# The resident daemon is a Haiku BApplication (needs libbe). We build it to prove the whole
# stack wires and links, but never run it here: running would start a real listening daemon.
ifeq ($(shell uname),Haiku)
campiello_daemon: $(SRV_DIR)/daemon_main.cpp $(SRV_DIR)/DaemonApp.cpp \
                  $(SRV_DIR)/HaikuPairingPrompt.cpp $(NODE_STACK) $(BRICOLA_SRCS)
	$(CXX) $(CXXFLAGS) -o $@ $^ -lbe $(LDLIBS)

daemon-build: campiello_daemon
	@echo "== campiello_daemon == linked (not run)"
else
daemon-build:
	@echo "== campiello_daemon == skipped (not Haiku)"
endif

clean:
	rm -f $(TESTS) campiello_daemon
	rm -rf cnp_fs_test.d servernode_test.d
	rm -f servernode_id.pem servernode_trust
