# Build and run the TLS identity test.
# Uses OpenSSL 3 (Apache-2.0). Portable: OpenSSL is available on Haiku and Linux.

CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -Werror -O2 -pthread
LDLIBS ?= -lssl -lcrypto
# On Haiku the socket API lives in libnetwork; on Linux it is in libc.
ifeq ($(shell uname),Haiku)
LDLIBS += -lnetwork
endif

TLS_DIR := ../../src/traghetto/tls
WIRE_DIR := ../../src/traghetto/wire
NET_DIR := ../../src/traghetto/transport

TESTS := test_identity test_tls_loopback

test_identity: test_identity.cpp $(TLS_DIR)/Identity.cpp $(TLS_DIR)/Fingerprint.cpp \
               $(TLS_DIR)/Identity.h
	$(CXX) $(CXXFLAGS) -o $@ test_identity.cpp $(TLS_DIR)/Identity.cpp \
	    $(TLS_DIR)/Fingerprint.cpp $(LDLIBS)

test_tls_loopback: test_tls_loopback.cpp $(TLS_DIR)/TlsConnection.cpp $(TLS_DIR)/Identity.cpp \
                   $(NET_DIR)/Connection.cpp $(WIRE_DIR)/FrameCodec.cpp $(WIRE_DIR)/Cbor.cpp \
                   $(WIRE_DIR)/Handshake.cpp
	$(CXX) $(CXXFLAGS) -o $@ test_tls_loopback.cpp $(TLS_DIR)/TlsConnection.cpp \
	    $(TLS_DIR)/Identity.cpp $(NET_DIR)/Connection.cpp $(WIRE_DIR)/FrameCodec.cpp \
	    $(WIRE_DIR)/Cbor.cpp $(WIRE_DIR)/Handshake.cpp $(LDLIBS)

.PHONY: check clean
check: $(TESTS)
	@for t in $(TESTS); do echo "== $$t =="; ./$$t || exit 1; done

clean:
	rm -f $(TESTS) test_identity.tmp.pem test_identity.fresh.pem
