# Build and run the transport loopback test.
# Pure standard C++17 + POSIX sockets, no Haiku dependency; runs in CI on any platform.

CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -Werror -O2 -pthread

# On Haiku the socket API lives in libnetwork; on Linux it is in libc.
LDLIBS ?=
ifeq ($(shell uname),Haiku)
LDLIBS += -lnetwork
endif

WIRE_DIR := ../../src/traghetto/wire
NET_DIR := ../../src/traghetto/transport

SRCS := test_loopback.cpp \
        $(NET_DIR)/Connection.cpp \
        $(WIRE_DIR)/FrameCodec.cpp \
        $(WIRE_DIR)/Cbor.cpp \
        $(WIRE_DIR)/Handshake.cpp \
        $(WIRE_DIR)/Listing.cpp \
        $(WIRE_DIR)/Attributes.cpp

test_loopback: $(SRCS)
	$(CXX) $(CXXFLAGS) -o $@ $(SRCS) $(LDLIBS)

.PHONY: check clean
check: test_loopback
	./test_loopback

clean:
	rm -f test_loopback
