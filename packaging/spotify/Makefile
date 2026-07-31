# Build the OPTIONAL Campiello Spotify Connect package (.hpkg).
#
# Ships the Spotify Connect info add-on + its handler manifest. Pure HTTP (no third-party dependency),
# so this add-on is itself MIT-clean; kept optional only because it is device-specific.
#
#   make package    # builds campiello_spotify-<version>-<arch>.hpkg
#   pkgman install ./campiello_spotify-0.1.0-1-x86_64.hpkg
#
# Haiku-only (needs the `package` tool).

VERSION := 0.1.0-3
ARCH := x86_64
PKG := campiello_spotify-$(VERSION)-$(ARCH).hpkg

CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O2

SPT := ../../optional/spotify
SRC := $(SPT)/campiello_spotify.cpp $(SPT)/SpotifyProbe.cpp
STAGE := stage

.PHONY: package clean
package: $(PKG)

$(PKG): campiello_spotify.PackageInfo campiello_spotify
	rm -rf $(STAGE)
	mkdir -p $(STAGE)/apps $(STAGE)/data/campiello/handlers
	cp campiello_spotify $(STAGE)/apps/campiello_spotify
	cp $(SPT)/spotify.handler $(STAGE)/data/campiello/handlers/spotify.handler
	package create -C $(STAGE) -i campiello_spotify.PackageInfo $(PKG)
	@echo "Built $(PKG)"

# The info app, with its app_signature resource so be_roster can launch it by signature.
campiello_spotify: $(SRC) $(SPT)/SpotifyProbe.h $(SPT)/Spotify.rdef
	$(CXX) $(CXXFLAGS) -o $@ $(SRC) -lbe -lnetwork
	rc -o campiello_spotify.rsrc $(SPT)/Spotify.rdef
	xres -o $@ campiello_spotify.rsrc
	mimeset -f $@
	rm -f campiello_spotify.rsrc

clean:
	rm -rf $(STAGE) campiello_spotify $(PKG)
