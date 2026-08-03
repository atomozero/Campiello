# Build the OPTIONAL Campiello eero package (.hpkg).
#
# Ships the eero info add-on + its handler manifest. Informational only (no local control API for
# eero), no third-party dependency, so this add-on is itself MIT-clean; kept optional only because it
# is device-specific.
#
#   make package    # builds campiello_eero-<version>-<arch>.hpkg
#   pkgman install ./campiello_eero-0.1.0-1-x86_64.hpkg
#
# Haiku-only (needs the `package` tool).

VERSION := 0.1.0-1
ARCH := x86_64
PKG := campiello_eero-$(VERSION)-$(ARCH).hpkg

CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O2

EERO := ../../optional/eero
SRC := $(EERO)/campiello_eero.cpp
STAGE := stage

.PHONY: package clean
package: $(PKG)

$(PKG): campiello_eero.PackageInfo campiello_eero
	rm -rf $(STAGE)
	mkdir -p $(STAGE)/apps $(STAGE)/data/campiello/handlers
	cp campiello_eero $(STAGE)/apps/campiello_eero
	cp $(EERO)/eero.handler $(STAGE)/data/campiello/handlers/eero.handler
	package create -C $(STAGE) -i campiello_eero.PackageInfo $(PKG)
	@echo "Built $(PKG)"

# The info app, with its app_signature resource so be_roster can launch it by signature.
campiello_eero: $(SRC) $(EERO)/Eero.rdef
	$(CXX) $(CXXFLAGS) -o $@ $(SRC) -lbe
	rc -o campiello_eero.rsrc $(EERO)/Eero.rdef
	xres -o $@ campiello_eero.rsrc
	mimeset -f $@
	rm -f campiello_eero.rsrc

clean:
	rm -rf $(STAGE) campiello_eero $(PKG)
