# Build the OPTIONAL Campiello Google Cast package (.hpkg).
#
# Ships the Cast (DIAL) panel + its handler manifest. Pure HTTP (no third-party dependency), so this
# add-on is itself MIT-clean; kept optional only because it is device-specific.
#
#   make package    # builds campiello_cast-<version>-<arch>.hpkg
#   pkgman install ./campiello_cast-0.1.0-1-x86_64.hpkg
#
# Haiku-only (needs the `package` tool).

VERSION := 0.1.0-2
ARCH := x86_64
PKG := campiello_cast-$(VERSION)-$(ARCH).hpkg

CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O2

CAST := ../../optional/cast
SRC := $(CAST)/campiello_cast.cpp $(CAST)/DialClient.cpp
STAGE := stage

.PHONY: package clean
package: $(PKG)

$(PKG): campiello_cast.PackageInfo campiello_cast
	rm -rf $(STAGE)
	mkdir -p $(STAGE)/apps $(STAGE)/data/campiello/handlers
	cp campiello_cast $(STAGE)/apps/campiello_cast
	cp $(CAST)/cast.handler $(STAGE)/data/campiello/handlers/cast.handler
	package create -C $(STAGE) -i campiello_cast.PackageInfo $(PKG)
	@echo "Built $(PKG)"

# The Cast panel, with its app_signature resource so be_roster can launch it by signature.
campiello_cast: $(SRC) $(CAST)/DialClient.h $(CAST)/Cast.rdef
	$(CXX) $(CXXFLAGS) -o $@ $(SRC) -lbe -lnetwork
	rc -o campiello_cast.rsrc $(CAST)/Cast.rdef
	xres -o $@ campiello_cast.rsrc
	mimeset -f $@
	rm -f campiello_cast.rsrc

clean:
	rm -rf $(STAGE) campiello_cast $(PKG)
