# Build and run the CNP wire tests (frame codec + CBOR codec).
# Pure standard C++17, no Haiku dependency, so this runs in CI on any platform.

CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -Werror -O2

WIRE_DIR := ../../src/traghetto/wire

TESTS := test_frame_codec test_cbor test_handshake test_listing test_error test_fileops \
         test_namespace test_attrops test_query

test_query: test_query.cpp $(WIRE_DIR)/Query.cpp $(WIRE_DIR)/Listing.cpp \
            $(WIRE_DIR)/Attributes.cpp $(WIRE_DIR)/Cbor.cpp $(WIRE_DIR)/Query.h \
            $(WIRE_DIR)/Listing.h $(WIRE_DIR)/Cbor.h $(WIRE_DIR)/Frame.h
	$(CXX) $(CXXFLAGS) -o $@ test_query.cpp $(WIRE_DIR)/Query.cpp $(WIRE_DIR)/Listing.cpp \
	    $(WIRE_DIR)/Attributes.cpp $(WIRE_DIR)/Cbor.cpp

test_frame_codec: test_frame_codec.cpp $(WIRE_DIR)/FrameCodec.cpp \
                  $(WIRE_DIR)/FrameCodec.h $(WIRE_DIR)/Frame.h
	$(CXX) $(CXXFLAGS) -o $@ test_frame_codec.cpp $(WIRE_DIR)/FrameCodec.cpp

test_cbor: test_cbor.cpp $(WIRE_DIR)/Cbor.cpp $(WIRE_DIR)/Cbor.h
	$(CXX) $(CXXFLAGS) -o $@ test_cbor.cpp $(WIRE_DIR)/Cbor.cpp

test_handshake: test_handshake.cpp $(WIRE_DIR)/Handshake.cpp $(WIRE_DIR)/Cbor.cpp \
                $(WIRE_DIR)/FrameCodec.cpp $(WIRE_DIR)/Handshake.h $(WIRE_DIR)/Cbor.h \
                $(WIRE_DIR)/Frame.h $(WIRE_DIR)/FrameCodec.h
	$(CXX) $(CXXFLAGS) -o $@ test_handshake.cpp $(WIRE_DIR)/Handshake.cpp \
	    $(WIRE_DIR)/Cbor.cpp $(WIRE_DIR)/FrameCodec.cpp

test_listing: test_listing.cpp $(WIRE_DIR)/Listing.cpp $(WIRE_DIR)/Attributes.cpp \
              $(WIRE_DIR)/Cbor.cpp $(WIRE_DIR)/FrameCodec.cpp $(WIRE_DIR)/Listing.h \
              $(WIRE_DIR)/Attributes.h $(WIRE_DIR)/Cbor.h $(WIRE_DIR)/Frame.h \
              $(WIRE_DIR)/FrameCodec.h
	$(CXX) $(CXXFLAGS) -o $@ test_listing.cpp $(WIRE_DIR)/Listing.cpp \
	    $(WIRE_DIR)/Attributes.cpp $(WIRE_DIR)/Cbor.cpp $(WIRE_DIR)/FrameCodec.cpp

test_error: test_error.cpp $(WIRE_DIR)/Error.cpp $(WIRE_DIR)/Cbor.cpp \
            $(WIRE_DIR)/FrameCodec.cpp $(WIRE_DIR)/Error.h $(WIRE_DIR)/Cbor.h \
            $(WIRE_DIR)/Frame.h $(WIRE_DIR)/FrameCodec.h
	$(CXX) $(CXXFLAGS) -o $@ test_error.cpp $(WIRE_DIR)/Error.cpp \
	    $(WIRE_DIR)/Cbor.cpp $(WIRE_DIR)/FrameCodec.cpp

test_fileops: test_fileops.cpp $(WIRE_DIR)/FileOps.cpp $(WIRE_DIR)/Listing.cpp \
              $(WIRE_DIR)/Attributes.cpp $(WIRE_DIR)/Cbor.cpp $(WIRE_DIR)/FrameCodec.cpp \
              $(WIRE_DIR)/FileOps.h $(WIRE_DIR)/Listing.h $(WIRE_DIR)/Cbor.h \
              $(WIRE_DIR)/Frame.h $(WIRE_DIR)/FrameCodec.h
	$(CXX) $(CXXFLAGS) -o $@ test_fileops.cpp $(WIRE_DIR)/FileOps.cpp \
	    $(WIRE_DIR)/Listing.cpp $(WIRE_DIR)/Attributes.cpp $(WIRE_DIR)/Cbor.cpp \
	    $(WIRE_DIR)/FrameCodec.cpp

test_namespace: test_namespace.cpp $(WIRE_DIR)/Namespace.cpp $(WIRE_DIR)/Listing.cpp \
                $(WIRE_DIR)/Attributes.cpp $(WIRE_DIR)/FileOps.cpp $(WIRE_DIR)/Cbor.cpp \
                $(WIRE_DIR)/Namespace.h $(WIRE_DIR)/Listing.h $(WIRE_DIR)/FileOps.h \
                $(WIRE_DIR)/Cbor.h $(WIRE_DIR)/Frame.h
	$(CXX) $(CXXFLAGS) -o $@ test_namespace.cpp $(WIRE_DIR)/Namespace.cpp \
	    $(WIRE_DIR)/Listing.cpp $(WIRE_DIR)/Attributes.cpp $(WIRE_DIR)/FileOps.cpp \
	    $(WIRE_DIR)/Cbor.cpp

test_attrops: test_attrops.cpp $(WIRE_DIR)/AttrOps.cpp $(WIRE_DIR)/Attributes.cpp \
              $(WIRE_DIR)/Listing.cpp $(WIRE_DIR)/FileOps.cpp $(WIRE_DIR)/Cbor.cpp \
              $(WIRE_DIR)/FrameCodec.cpp $(WIRE_DIR)/AttrOps.h $(WIRE_DIR)/Attributes.h \
              $(WIRE_DIR)/Listing.h $(WIRE_DIR)/FileOps.h $(WIRE_DIR)/Cbor.h $(WIRE_DIR)/Frame.h
	$(CXX) $(CXXFLAGS) -o $@ test_attrops.cpp $(WIRE_DIR)/AttrOps.cpp \
	    $(WIRE_DIR)/Attributes.cpp $(WIRE_DIR)/Listing.cpp $(WIRE_DIR)/FileOps.cpp \
	    $(WIRE_DIR)/Cbor.cpp $(WIRE_DIR)/FrameCodec.cpp

.PHONY: check clean
check: $(TESTS)
	@for t in $(TESTS); do echo "== $$t =="; ./$$t || exit 1; done

clean:
	rm -f $(TESTS)
