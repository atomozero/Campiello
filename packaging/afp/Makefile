# Build the OPTIONAL Campiello AFP package (.hpkg).
#
# Ships the AFP info probe + its handler manifest. Pure DSI over TCP (no third-party dependency), so
# this add-on is itself MIT-clean; kept optional only because it is device-specific.
#
#   make package    # builds campiello_afp-<version>-<arch>.hpkg
#   pkgman install ./campiello_afp-0.1.0-1-x86_64.hpkg
#
# Haiku-only (needs the `package` tool).

VERSION := 0.1.0-2
ARCH := x86_64
PKG := campiello_afp-$(VERSION)-$(ARCH).hpkg

CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O2

AFP := ../../optional/afp
SRC := $(AFP)/campiello_afp.cpp $(AFP)/AfpProbe.cpp
STAGE := stage

.PHONY: package clean
package: $(PKG)

$(PKG): campiello_afp.PackageInfo campiello_afp
	rm -rf $(STAGE)
	mkdir -p $(STAGE)/apps $(STAGE)/data/campiello/handlers
	cp campiello_afp $(STAGE)/apps/campiello_afp
	cp $(AFP)/afp.handler $(STAGE)/data/campiello/handlers/afp.handler
	package create -C $(STAGE) -i campiello_afp.PackageInfo $(PKG)
	@echo "Built $(PKG)"

# The info probe, with its app_signature resource so be_roster can launch it by signature.
campiello_afp: $(SRC) $(AFP)/AfpProbe.h $(AFP)/Afp.rdef
	$(CXX) $(CXXFLAGS) -o $@ $(SRC) -lbe -lnetwork
	rc -o campiello_afp.rsrc $(AFP)/Afp.rdef
	xres -o $@ campiello_afp.rsrc
	mimeset -f $@
	rm -f campiello_afp.rsrc

clean:
	rm -rf $(STAGE) campiello_afp $(PKG)
