# Build and run the request/response dispatcher test.
# Pure standard C++17 + POSIX sockets + std::thread, no Haiku dependency; CI-portable.

CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -Werror -O2 -pthread
LDLIBS ?=
ifeq ($(shell uname),Haiku)
LDLIBS += -lnetwork
endif

WIRE_DIR := ../../src/traghetto/wire
NET_DIR := ../../src/traghetto/transport
DISP_DIR := ../../src/traghetto/dispatch

SRCS := test_dispatch.cpp \
        $(DISP_DIR)/Dispatch.cpp $(DISP_DIR)/FrameWriter.cpp \
        $(NET_DIR)/Connection.cpp \
        $(WIRE_DIR)/FrameCodec.cpp \
        $(WIRE_DIR)/Cbor.cpp \
        $(WIRE_DIR)/Handshake.cpp \
        $(WIRE_DIR)/Listing.cpp \
        $(WIRE_DIR)/Attributes.cpp \
        $(WIRE_DIR)/Error.cpp

test_dispatch: $(SRCS)
	$(CXX) $(CXXFLAGS) -o $@ $(SRCS) $(LDLIBS)

test_framewriter: test_framewriter.cpp $(DISP_DIR)/FrameWriter.cpp $(DISP_DIR)/FrameWriter.h
	$(CXX) $(CXXFLAGS) -o $@ test_framewriter.cpp $(DISP_DIR)/FrameWriter.cpp $(LDLIBS)

.PHONY: check clean
check: test_dispatch test_framewriter
	./test_dispatch
	./test_framewriter

clean:
	rm -f test_dispatch test_framewriter
