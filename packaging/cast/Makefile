# Build the OPTIONAL Campiello Google Cast package (.hpkg).
#
# Ships the Cast (DIAL) panel + its handler manifest. Pure HTTP (no third-party dependency), so this
# add-on is itself MIT-clean; kept optional only because it is device-specific.
#
#   make package    # builds campiello_cast-<version>-<arch>.hpkg
#   pkgman install ./campiello_cast-0.1.0-1-x86_64.hpkg
#
# Haiku-only (needs the `package` tool).

VERSION := 0.5.1-1
ARCH := x86_64
PKG := campiello_cast-$(VERSION)-$(ARCH).hpkg

CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O2

CAST := ../../optional/cast
SRC := $(CAST)/campiello_cast.cpp $(CAST)/DialClient.cpp $(CAST)/CastChannel.cpp \
       $(CAST)/MediaServer.cpp $(CAST)/MirrorSession.cpp $(CAST)/CastMirror.cpp \
       $(CAST)/CastTransport.cpp $(CAST)/VpxEncoder.cpp
STAGE := stage

.PHONY: package clean test
package: $(PKG)

$(PKG): campiello_cast.PackageInfo campiello_cast
	rm -rf $(STAGE)
	mkdir -p $(STAGE)/apps $(STAGE)/data/campiello/handlers
	cp campiello_cast $(STAGE)/apps/campiello_cast
	cp $(CAST)/cast.handler $(STAGE)/data/campiello/handlers/cast.handler
	package create -C $(STAGE) -i campiello_cast.PackageInfo $(PKG)
	@echo "Built $(PKG)"

# The Cast panel, with its app_signature resource so be_roster can launch it by signature.
# Links OpenSSL for the CASTv2 TLS channel (kept optional; the MIT core never depends on it).
CAST_HDRS := $(CAST)/DialClient.h $(CAST)/CastChannel.h $(CAST)/MediaServer.h \
             $(CAST)/MirrorSession.h $(CAST)/CastMirror.h $(CAST)/CastTransport.h $(CAST)/VpxEncoder.h

campiello_cast: $(SRC) $(CAST_HDRS) $(CAST)/Cast.rdef
	$(CXX) $(CXXFLAGS) -o $@ $(SRC) -lbe -ltracker -ltranslation -lnetwork -lssl -lcrypto -lvpx
	rc -o campiello_cast.rsrc $(CAST)/Cast.rdef
	xres -o $@ campiello_cast.rsrc
	mimeset -f $@
	rm -f campiello_cast.rsrc

# Protocol codec + media-server + mirroring-negotiation unit tests (loopback sockets only).
test:
	$(CXX) $(CXXFLAGS) -o test_cast $(CAST)/test_cast.cpp $(CAST)/CastChannel.cpp \
		$(CAST)/DialClient.cpp -lssl -lcrypto -lnetwork
	./test_cast
	$(CXX) $(CXXFLAGS) -o test_mediaserver $(CAST)/test_mediaserver.cpp $(CAST)/MediaServer.cpp -lnetwork
	./test_mediaserver
	$(CXX) $(CXXFLAGS) -o test_mirror $(CAST)/test_mirror.cpp $(CAST)/CastMirror.cpp \
		$(CAST)/CastChannel.cpp -lssl -lcrypto -lnetwork
	./test_mirror
	$(CXX) $(CXXFLAGS) -o test_transport $(CAST)/test_transport.cpp $(CAST)/CastTransport.cpp \
		-lssl -lcrypto -lnetwork
	./test_transport

# Dev tool: run the Cast Streaming milestone-1 handshake against a real device (needs a Cast video
# receiver on the LAN). Build only; run as ./mirror_probe <ip>.
probe:
	$(CXX) $(CXXFLAGS) -o mirror_probe $(CAST)/mirror_probe.cpp $(CAST)/CastMirror.cpp \
		$(CAST)/CastChannel.cpp -lssl -lcrypto -lnetwork
	@echo "Built mirror_probe. Run: ./mirror_probe <ip-del-chromecast>"

# Milestone-2 proof: VP8 encode + decode round-trip. Needs libvpx_devel (HaikuPorts). Kept out of the
# default `test` so the suite builds without libvpx.
vpxtest:
	$(CXX) $(CXXFLAGS) -o test_vpx $(CAST)/test_vpx.cpp $(CAST)/VpxEncoder.cpp -lvpx
	./test_vpx

# Milestone-4 dev tool: mirror this desktop to a real Cast device (needs libvpx + a physical Cast
# video receiver). Build only; run as ./mirror_send <ip> [seconds] [fps].
mirrorsend:
	$(CXX) $(CXXFLAGS) -o mirror_send $(CAST)/mirror_send.cpp $(CAST)/MirrorSession.cpp \
		$(CAST)/CastChannel.cpp $(CAST)/CastMirror.cpp $(CAST)/CastTransport.cpp \
		$(CAST)/VpxEncoder.cpp -lbe -lvpx -lssl -lcrypto -lnetwork
	@echo "Built mirror_send. Run: ./mirror_send <ip-del-chromecast> [secondi] [fps]"

# Dev diagnostic: send frames and print the receiver's CAST ack_frame_id/loss_count (no TV needed) to
# tell reception from decode problems. Needs libvpx + a real Cast video receiver.
rtcpprobe:
	$(CXX) $(CXXFLAGS) -o mirror_rtcp_probe $(CAST)/mirror_rtcp_probe.cpp $(CAST)/MirrorSession.cpp \
		$(CAST)/CastChannel.cpp $(CAST)/CastMirror.cpp $(CAST)/CastTransport.cpp \
		$(CAST)/VpxEncoder.cpp -lbe -lvpx -lssl -lcrypto -lnetwork
	@echo "Built mirror_rtcp_probe. Run: ./mirror_rtcp_probe <ip>"

clean:
	rm -rf $(STAGE) campiello_cast test_cast test_mediaserver test_mirror test_transport \
		mirror_probe mirror_send test_vpx $(PKG)
