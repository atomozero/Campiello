# Build the OPTIONAL Campiello DAAP music-library package (.hpkg).
#
# Ships the DAAP library browser + its handler manifest. Pure HTTP + a hand-rolled DMAP parser (no
# third-party dependency), so this add-on is itself MIT-clean; kept optional only because it is
# device-specific.
#
#   make package    # builds campiello_daap-<version>-<arch>.hpkg
#   pkgman install ./campiello_daap-0.1.0-1-x86_64.hpkg
#
# Haiku-only (needs the `package` tool).

VERSION := 0.1.0-2
ARCH := x86_64
PKG := campiello_daap-$(VERSION)-$(ARCH).hpkg

CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O2

DAAP := ../../optional/daap
SRC := $(DAAP)/campiello_daap.cpp $(DAAP)/DaapClient.cpp
STAGE := stage

.PHONY: package clean
package: $(PKG)

$(PKG): campiello_daap.PackageInfo campiello_daap
	rm -rf $(STAGE)
	mkdir -p $(STAGE)/apps $(STAGE)/data/campiello/handlers
	cp campiello_daap $(STAGE)/apps/campiello_daap
	cp $(DAAP)/daap.handler $(STAGE)/data/campiello/handlers/daap.handler
	package create -C $(STAGE) -i campiello_daap.PackageInfo $(PKG)
	@echo "Built $(PKG)"

# The library browser, with its app_signature resource so be_roster can launch it by signature.
campiello_daap: $(SRC) $(DAAP)/DaapClient.h $(DAAP)/Daap.rdef
	$(CXX) $(CXXFLAGS) -o $@ $(SRC) -lbe -lnetwork
	rc -o campiello_daap.rsrc $(DAAP)/Daap.rdef
	xres -o $@ campiello_daap.rsrc
	mimeset -f $@
	rm -f campiello_daap.rsrc

clean:
	rm -rf $(STAGE) campiello_daap $(PKG)
