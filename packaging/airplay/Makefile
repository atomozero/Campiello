# Build the OPTIONAL Campiello AirPlay package (.hpkg).
#
# Ships the AirPlay info add-on + its handler manifest. No crypto, no third-party dependency, so this
# add-on is itself MIT-clean; kept optional only because it is device-specific.
#
#   make package    # builds campiello_airplay-<version>-<arch>.hpkg
#   pkgman install ./campiello_airplay-0.1.0-1-x86_64.hpkg
#
# Haiku-only (needs the `package` tool).

VERSION := 0.1.0-2
ARCH := x86_64
PKG := campiello_airplay-$(VERSION)-$(ARCH).hpkg

CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O2

AP := ../../optional/airplay
SRC := $(AP)/campiello_airplay.cpp $(AP)/AirplayInfo.cpp
STAGE := stage

.PHONY: package clean
package: $(PKG)

$(PKG): campiello_airplay.PackageInfo campiello_airplay
	rm -rf $(STAGE)
	mkdir -p $(STAGE)/apps $(STAGE)/data/campiello/handlers
	cp campiello_airplay $(STAGE)/apps/campiello_airplay
	cp $(AP)/airplay.handler $(STAGE)/data/campiello/handlers/airplay.handler
	package create -C $(STAGE) -i campiello_airplay.PackageInfo $(PKG)
	@echo "Built $(PKG)"

# The info app, with its app_signature resource so be_roster can launch it by signature.
campiello_airplay: $(SRC) $(AP)/AirplayInfo.h $(AP)/Airplay.rdef
	$(CXX) $(CXXFLAGS) -o $@ $(SRC) -lbe
	rc -o campiello_airplay.rsrc $(AP)/Airplay.rdef
	xres -o $@ campiello_airplay.rsrc
	mimeset -f $@
	rm -f campiello_airplay.rsrc

clean:
	rm -rf $(STAGE) campiello_airplay $(PKG)
