# Build the OPTIONAL Campiello Google Cast package (.hpkg).
#
# Ships the Cast (DIAL) panel + its handler manifest. Pure HTTP (no third-party dependency), so this
# add-on is itself MIT-clean; kept optional only because it is device-specific.
#
#   make package    # builds campiello_cast-<version>-<arch>.hpkg
#   pkgman install ./campiello_cast-0.1.0-1-x86_64.hpkg
#
# Haiku-only (needs the `package` tool).

VERSION := 0.4.0-1
ARCH := x86_64
PKG := campiello_cast-$(VERSION)-$(ARCH).hpkg

CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O2

CAST := ../../optional/cast
SRC := $(CAST)/campiello_cast.cpp $(CAST)/DialClient.cpp $(CAST)/CastChannel.cpp \
       $(CAST)/MediaServer.cpp
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
campiello_cast: $(SRC) $(CAST)/DialClient.h $(CAST)/CastChannel.h $(CAST)/MediaServer.h $(CAST)/Cast.rdef
	$(CXX) $(CXXFLAGS) -o $@ $(SRC) -lbe -ltracker -ltranslation -lnetwork -lssl -lcrypto
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

# Dev tool: run the Cast Streaming milestone-1 handshake against a real device (needs a Cast video
# receiver on the LAN). Build only; run as ./mirror_probe <ip>.
probe:
	$(CXX) $(CXXFLAGS) -o mirror_probe $(CAST)/mirror_probe.cpp $(CAST)/CastMirror.cpp \
		$(CAST)/CastChannel.cpp -lssl -lcrypto -lnetwork
	@echo "Built mirror_probe. Run: ./mirror_probe <ip-del-chromecast>"

clean:
	rm -rf $(STAGE) campiello_cast test_cast test_mediaserver test_mirror mirror_probe $(PKG)
