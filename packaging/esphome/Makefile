# Build the OPTIONAL Campiello ESPHome package (.hpkg).
#
# Ships the ESPHome info add-on + its handler manifest. No network and no third-party dependency (it
# reads the mDNS TXT and opens the web UI via be_roster), so this add-on is itself MIT-clean; kept
# optional only because it is device-specific.
#
#   make package    # builds campiello_esphome-<version>-<arch>.hpkg
#   pkgman install ./campiello_esphome-0.1.0-1-x86_64.hpkg
#
# Haiku-only (needs the `package` tool).

VERSION := 0.1.0-1
ARCH := x86_64
PKG := campiello_esphome-$(VERSION)-$(ARCH).hpkg

CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O2

ESP := ../../optional/esphome
SRC := $(ESP)/campiello_esphome.cpp
STAGE := stage

.PHONY: package clean
package: $(PKG)

$(PKG): campiello_esphome.PackageInfo campiello_esphome
	rm -rf $(STAGE)
	mkdir -p $(STAGE)/apps $(STAGE)/data/campiello/handlers
	cp campiello_esphome $(STAGE)/apps/campiello_esphome
	cp $(ESP)/esphome.handler $(STAGE)/data/campiello/handlers/esphome.handler
	package create -C $(STAGE) -i campiello_esphome.PackageInfo $(PKG)
	@echo "Built $(PKG)"

# The info app, with its app_signature resource so be_roster can launch it by signature.
campiello_esphome: $(SRC) $(ESP)/Esphome.rdef
	$(CXX) $(CXXFLAGS) -o $@ $(SRC) -lbe
	rc -o campiello_esphome.rsrc $(ESP)/Esphome.rdef
	xres -o $@ campiello_esphome.rsrc
	mimeset -f $@
	rm -f campiello_esphome.rsrc

clean:
	rm -rf $(STAGE) campiello_esphome $(PKG)
