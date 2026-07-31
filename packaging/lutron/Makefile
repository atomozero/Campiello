# Build the OPTIONAL Campiello Lutron package (.hpkg).
#
# Ships the Lutron info add-on + its handler manifest. Informational only (control needs Secure LEAP
# TLS pairing), no third-party dependency, so this add-on is itself MIT-clean; kept optional only
# because it is device-specific.
#
#   make package    # builds campiello_lutron-<version>-<arch>.hpkg
#   pkgman install ./campiello_lutron-0.1.0-1-x86_64.hpkg
#
# Haiku-only (needs the `package` tool).

VERSION := 0.1.0-2
ARCH := x86_64
PKG := campiello_lutron-$(VERSION)-$(ARCH).hpkg

CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O2

LUT := ../../optional/lutron
SRC := $(LUT)/campiello_lutron.cpp
STAGE := stage

.PHONY: package clean
package: $(PKG)

$(PKG): campiello_lutron.PackageInfo campiello_lutron
	rm -rf $(STAGE)
	mkdir -p $(STAGE)/apps $(STAGE)/data/campiello/handlers
	cp campiello_lutron $(STAGE)/apps/campiello_lutron
	cp $(LUT)/lutron.handler $(STAGE)/data/campiello/handlers/lutron.handler
	package create -C $(STAGE) -i campiello_lutron.PackageInfo $(PKG)
	@echo "Built $(PKG)"

# The info app, with its app_signature resource so be_roster can launch it by signature.
campiello_lutron: $(SRC) $(LUT)/Lutron.rdef
	$(CXX) $(CXXFLAGS) -o $@ $(SRC) -lbe
	rc -o campiello_lutron.rsrc $(LUT)/Lutron.rdef
	xres -o $@ campiello_lutron.rsrc
	mimeset -f $@
	rm -f campiello_lutron.rsrc

clean:
	rm -rf $(STAGE) campiello_lutron $(PKG)
