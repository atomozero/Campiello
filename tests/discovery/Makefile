# Build and run the discovery-filesystem tests.
# - test_pathrouter is pure standard C++ (no Haiku, no network).
# - test_connmanager runs the full native stack over loopback (OpenSSL + sockets + threads),
#   like tests/server: a real ServerNode plus the discovery ConnectionManager and PathRouter.

CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -Werror -O2 -pthread
LDLIBS ?= -lssl -lcrypto
ifeq ($(shell uname),Haiku)
LDLIBS += -lnetwork
endif

DISC_DIR := ../../src/fondamenta/discovery
BE_DIR := ../../src/fondamenta/backend
FUSE_DIR := ../../src/fondamenta/fuse
MDNS_DIR := ../../src/bricola/mdns
WIRE_DIR := ../../src/traghetto/wire
NET_DIR := ../../src/traghetto/transport
TLS_DIR := ../../src/traghetto/tls
DISP_DIR := ../../src/traghetto/dispatch
SRV_DIR := ../../src/traghetto/server
TRUST_DIR := ../../src/traghetto/trust
FUSE_HEADERS := /boot/system/develop/headers/private/userlandfs/fuse

# The full native stack ConnectionManager needs (client TLS + a real server to talk to).
STACK := $(DISC_DIR)/ConnectionManager.cpp $(DISC_DIR)/PathRouter.cpp $(DISC_DIR)/QueryAggregator.cpp \
         $(BE_DIR)/CnpBackend.cpp \
         $(SRV_DIR)/ServerNode.cpp $(SRV_DIR)/GatedHandler.cpp $(SRV_DIR)/FileServer.cpp \
         $(SRV_DIR)/Daemon.cpp $(DISP_DIR)/Dispatch.cpp $(DISP_DIR)/FrameWriter.cpp $(NET_DIR)/Connection.cpp \
         $(TLS_DIR)/TlsConnection.cpp $(TLS_DIR)/Identity.cpp $(TLS_DIR)/Fingerprint.cpp \
         $(TRUST_DIR)/Pairing.cpp $(TRUST_DIR)/TokenBucket.cpp $(TRUST_DIR)/TrustStore.cpp \
         $(TRUST_DIR)/Paths.cpp \
         $(WIRE_DIR)/FrameCodec.cpp $(WIRE_DIR)/Cbor.cpp $(WIRE_DIR)/Handshake.cpp \
         $(WIRE_DIR)/Error.cpp $(WIRE_DIR)/Listing.cpp $(WIRE_DIR)/Attributes.cpp \
         $(WIRE_DIR)/FileOps.cpp $(WIRE_DIR)/Namespace.cpp $(WIRE_DIR)/AttrOps.cpp

TESTS := test_pathrouter test_connmanager

test_pathrouter: test_pathrouter.cpp $(DISC_DIR)/PathRouter.cpp $(DISC_DIR)/QueryAggregator.cpp \
                 $(DISC_DIR)/PathRouter.h $(DISC_DIR)/QueryAggregator.h
	$(CXX) $(CXXFLAGS) -o $@ test_pathrouter.cpp $(DISC_DIR)/PathRouter.cpp \
	    $(DISC_DIR)/QueryAggregator.cpp

test_connmanager: test_connmanager.cpp $(STACK)
	$(CXX) $(CXXFLAGS) -o $@ test_connmanager.cpp $(STACK) $(LDLIBS)

# The discovery userlandfs add-on: Bricola + ConnectionManager + PathRouter over the FUSE front
# end (client-only, no ServerNode). Haiku-only; linked to verify it assembles, not mounted here.
NET_ADDON := $(DISC_DIR)/campiello_net_main.cpp $(DISC_DIR)/BricolaDirectory.cpp \
             $(DISC_DIR)/ConnectionManager.cpp $(DISC_DIR)/PathRouter.cpp \
             $(FUSE_DIR)/CampielloFuse.cpp $(FUSE_DIR)/FuseTranslate.cpp \
             $(BE_DIR)/CnpBackend.cpp \
             $(TLS_DIR)/TlsConnection.cpp $(TLS_DIR)/Identity.cpp $(TLS_DIR)/Fingerprint.cpp \
             $(TRUST_DIR)/TrustStore.cpp $(NET_DIR)/Connection.cpp $(DISP_DIR)/Dispatch.cpp $(DISP_DIR)/FrameWriter.cpp \
             $(WIRE_DIR)/FrameCodec.cpp $(WIRE_DIR)/Cbor.cpp $(WIRE_DIR)/Handshake.cpp \
             $(WIRE_DIR)/Error.cpp $(WIRE_DIR)/Listing.cpp $(WIRE_DIR)/Attributes.cpp \
             $(WIRE_DIR)/FileOps.cpp $(WIRE_DIR)/Namespace.cpp $(WIRE_DIR)/AttrOps.cpp \
             $(MDNS_DIR)/Bricola.cpp $(MDNS_DIR)/Responder.cpp $(MDNS_DIR)/Browser.cpp \
             $(MDNS_DIR)/PeerTable.cpp $(MDNS_DIR)/MdnsSocket.cpp $(MDNS_DIR)/MdnsWire.cpp

.PHONY: check clean net-addon
ifeq ($(shell uname),Haiku)
check: $(TESTS) net-addon
	@for t in $(TESTS); do echo "== $$t =="; ./$$t || exit 1; done

net-addon: $(NET_ADDON)
	$(CXX) $(CXXFLAGS) -shared -fPIC -D_FILE_OFFSET_BITS=64 -I$(FUSE_HEADERS) \
	    -o campiello_net $(NET_ADDON) -luserlandfs_fuse $(LDLIBS)
	@echo "== campiello_net == discovery add-on linked (not mounted)"
else
check: $(TESTS)
	@for t in $(TESTS); do echo "== $$t =="; ./$$t || exit 1; done
	@echo "== campiello_net == skipped (not Haiku)"
endif

clean:
	rm -f $(TESTS) campiello_net
	rm -rf connmgr_test.d
	rm -f connmgr_server_id.pem connmgr_server_trust connmgr_client_id.pem connmgr_client_trust
