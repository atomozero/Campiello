# Build the OPTIONAL Campiello Google Cast package (.hpkg).
#
# Ships the Cast (DIAL) panel + its handler manifest. Pure HTTP (no third-party dependency), so this
# add-on is itself MIT-clean; kept optional only because it is device-specific.
#
#   make package    # builds campiello_cast-<version>-<arch>.hpkg
#   pkgman install ./campiello_cast-0.1.0-1-x86_64.hpkg
#
# Haiku-only (needs the `package` tool).

VERSION := 0.6.0-2
ARCH := x86_64
PKG := campiello_cast-$(VERSION)-$(ARCH).hpkg

CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O2

CAST := ../../optional/cast
SRC := $(CAST)/campiello_cast.cpp $(CAST)/DialClient.cpp $(CAST)/CastChannel.cpp \
       $(CAST)/MediaServer.cpp $(CAST)/MirrorSession.cpp $(CAST)/CastMirror.cpp \
       $(CAST)/CastTransport.cpp $(CAST)/VpxEncoder.cpp $(CAST)/OpusEncoder.cpp \
       $(CAST)/AudioCapture.cpp
STAGE := stage

# Localization (Haiku Locale Kit). Source strings are Italian and show when no catalog matches the
# user's language; the catalogs here translate them. CATALOG_DIR is the signature with the
# "application/" prefix dropped, which is where BLocaleRoster looks (see the system catalogs).
CATALOG_SIG := application/x-vnd.Campiello-cast
CATALOG_DIR := x-vnd.Campiello-cast
LANGS := en
CATALOGS := $(LANGS:%=%.catalog)

.PHONY: package clean test
package: $(PKG)

$(PKG): campiello_cast.PackageInfo campiello_cast $(CATALOGS)
	rm -rf $(STAGE)
	mkdir -p $(STAGE)/apps $(STAGE)/data/campiello/handlers
	mkdir -p $(STAGE)/data/locale/catalogs/$(CATALOG_DIR)
	cp campiello_cast $(STAGE)/apps/campiello_cast
	cp $(CAST)/cast.handler $(STAGE)/data/campiello/handlers/cast.handler
	for l in $(LANGS); do cp $$l.catalog $(STAGE)/data/locale/catalogs/$(CATALOG_DIR)/$$l.catalog; done
	package create -C $(STAGE) -i campiello_cast.PackageInfo $(PKG)
	@echo "Built $(PKG)"

# The Cast panel, with its app_signature resource so be_roster can launch it by signature.
# Links OpenSSL for the CASTv2 TLS channel (kept optional; the MIT core never depends on it).
# liblocalestub so B_TRANSLATE resolves the app's catalog at runtime.
CAST_HDRS := $(CAST)/DialClient.h $(CAST)/CastChannel.h $(CAST)/MediaServer.h \
             $(CAST)/MirrorSession.h $(CAST)/CastMirror.h $(CAST)/CastTransport.h \
             $(CAST)/VpxEncoder.h $(CAST)/OpusEncoder.h $(CAST)/AudioCapture.h

campiello_cast: $(SRC) $(CAST_HDRS) $(CAST)/Cast.rdef
	$(CXX) $(CXXFLAGS) -o $@ $(SRC) -lbe -ltracker -ltranslation -lmedia -lnetwork \
		-lssl -lcrypto -lvpx -lopus -llocalestub
	rc -o campiello_cast.rsrc $(CAST)/Cast.rdef
	xres -o $@ campiello_cast.rsrc
	mimeset -f $@
	rm -f campiello_cast.rsrc

# Build a catalog from its checked-in catkeys. To refresh the catkeys after changing strings, see
# `make catkeys` below.
%.catalog: $(CAST)/locales/%.catkeys
	linkcatkeys -l $* -s $(CATALOG_SIG) -o $@ $<

# Regenerate the catkeys template from the source (run after adding/changing B_TRANSLATE strings):
# preprocess in collecting mode (-P drops the line markers that break the key scanner), then collect.
# Only campiello_cast.cpp carries user-facing strings; the other translation units in $(SRC) are
# protocol/transport/encoder internals with no B_TRANSLATE calls, so preprocessing just the one file
# is enough to collect every key.
.PHONY: catkeys
catkeys:
	$(CXX) -DB_COLLECTING_CATKEYS -E -P -x c++ -std=c++17 $(CAST)/campiello_cast.cpp > cast.pre
	collectcatkeys -s $(CATALOG_SIG) -l it -o $(CAST)/locales/it.catkeys cast.pre
	rm -f cast.pre
	@echo "Refreshed locales/it.catkeys; merge new keys into the other locales/*.catkeys by hand."

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

# Audio-milestone proof: Opus encode + decode round-trip. Needs opus_devel (HaikuPorts). Kept out of
# the default `test` so the suite builds without libopus.
opustest:
	$(CXX) $(CXXFLAGS) -o test_opus $(CAST)/test_opus.cpp $(CAST)/OpusEncoder.cpp -lopus -lm
	./test_opus

# Milestone-4 dev tool: mirror this desktop to a real Cast device (needs libvpx + a physical Cast
# video receiver). Build only; run as ./mirror_send <ip> [seconds] [fps].
mirrorsend:
	$(CXX) $(CXXFLAGS) -o mirror_send $(CAST)/mirror_send.cpp $(CAST)/MirrorSession.cpp \
		$(CAST)/CastChannel.cpp $(CAST)/CastMirror.cpp $(CAST)/CastTransport.cpp \
		$(CAST)/VpxEncoder.cpp $(CAST)/OpusEncoder.cpp $(CAST)/AudioCapture.cpp \
		-lbe -lmedia -lvpx -lopus -lssl -lcrypto -lnetwork
	@echo "Built mirror_send. Run: ./mirror_send <ip-del-chromecast> [secondi] [fps]"

# Dev diagnostic: send frames and print the receiver's CAST ack_frame_id/loss_count (no TV needed) to
# tell reception from decode problems. Needs libvpx + a real Cast video receiver.
rtcpprobe:
	$(CXX) $(CXXFLAGS) -o mirror_rtcp_probe $(CAST)/mirror_rtcp_probe.cpp $(CAST)/MirrorSession.cpp \
		$(CAST)/CastChannel.cpp $(CAST)/CastMirror.cpp $(CAST)/CastTransport.cpp \
		$(CAST)/VpxEncoder.cpp $(CAST)/OpusEncoder.cpp -lbe -lvpx -lopus -lssl -lcrypto -lnetwork
	@echo "Built mirror_rtcp_probe. Run: ./mirror_rtcp_probe <ip>"

clean:
	rm -rf $(STAGE) campiello_cast test_cast test_mediaserver test_mirror test_transport test_opus \
		mirror_probe mirror_send test_vpx $(PKG) $(CATALOGS) cast.pre
