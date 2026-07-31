# Build and run the client-side CnpBackend loopback test.
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
BE_DIR := ../../src/fondamenta/backend

SRCS := test_cnpbackend.cpp \
        $(BE_DIR)/CnpBackend.cpp $(BE_DIR)/LiveQueryClient.cpp \
        $(SRV_DIR)/FileServer.cpp $(SRV_DIR)/Daemon.cpp \
        $(DISP_DIR)/Dispatch.cpp $(DISP_DIR)/FrameWriter.cpp $(NET_DIR)/Connection.cpp \
        $(TLS_DIR)/TlsConnection.cpp $(TLS_DIR)/Identity.cpp \
        $(WIRE_DIR)/FrameCodec.cpp $(WIRE_DIR)/Cbor.cpp $(WIRE_DIR)/Handshake.cpp \
        $(WIRE_DIR)/Listing.cpp $(WIRE_DIR)/Attributes.cpp $(WIRE_DIR)/FileOps.cpp \
        $(WIRE_DIR)/Namespace.cpp $(WIRE_DIR)/AttrOps.cpp $(WIRE_DIR)/Error.cpp \
        $(WIRE_DIR)/Query.cpp

TESTS := test_cnpbackend test_sftpknownhosts test_sftpbackend

# FileServer runs a Haiku BQuery for the QUERY path, so this test links libbe.
test_cnpbackend: $(SRCS)
	$(CXX) $(CXXFLAGS) -o $@ $(SRCS) -lbe $(LDLIBS)

# The SFTP host-key store is pure standard C++: no OpenSSL, no sockets, no libssh2.
test_sftpknownhosts: test_sftpknownhosts.cpp $(BE_DIR)/SftpKnownHosts.cpp $(BE_DIR)/SftpKnownHosts.h
	$(CXX) $(CXXFLAGS) -o $@ test_sftpknownhosts.cpp $(BE_DIR)/SftpKnownHosts.cpp

# The SFTP backend integration test links libssh2; it SKIPs at run time without a server.
test_sftpbackend: test_sftpbackend.cpp $(BE_DIR)/SftpBackend.cpp $(BE_DIR)/SftpKnownHosts.cpp \
                  $(BE_DIR)/SftpBackend.h $(BE_DIR)/SftpKnownHosts.h
	$(CXX) $(CXXFLAGS) -o $@ test_sftpbackend.cpp $(BE_DIR)/SftpBackend.cpp \
	    $(BE_DIR)/SftpKnownHosts.cpp -lssh2 $(LDLIBS)

.PHONY: check clean
check: $(TESTS)
	@for t in $(TESTS); do echo "== $$t =="; ./$$t || exit 1; done

clean:
	rm -f $(TESTS)
	rm -rf cnp_be_test.d
	rm -f sftp_known_hosts_test.tmp
