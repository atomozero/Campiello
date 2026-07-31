# Build the OPTIONAL Campiello Matter package (.hpkg).
#
# Ships the Matter info add-on + its handler manifest. No crypto, no third-party dependency, so this
# add-on is itself MIT-clean; kept optional only because it is device-specific.
#
#   make package    # builds campiello_matter-<version>-<arch>.hpkg
#   pkgman install ./campiello_matter-0.1.0-1-x86_64.hpkg
#
# Haiku-only (needs the `package` tool).

VERSION := 0.1.0-2
ARCH := x86_64
PKG := campiello_matter-$(VERSION)-$(ARCH).hpkg

CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O2

MT := ../../optional/matter
SRC := $(MT)/campiello_matter.cpp $(MT)/MatterInfo.cpp
STAGE := stage

.PHONY: package clean
package: $(PKG)

$(PKG): campiello_matter.PackageInfo campiello_matter
	rm -rf $(STAGE)
	mkdir -p $(STAGE)/apps $(STAGE)/data/campiello/handlers
	cp campiello_matter $(STAGE)/apps/campiello_matter
	cp $(MT)/matter.handler $(STAGE)/data/campiello/handlers/matter.handler
	package create -C $(STAGE) -i campiello_matter.PackageInfo $(PKG)
	@echo "Built $(PKG)"

# The info app, with its app_signature resource so be_roster can launch it by signature.
campiello_matter: $(SRC) $(MT)/MatterInfo.h $(MT)/Matter.rdef
	$(CXX) $(CXXFLAGS) -o $@ $(SRC) -lbe
	rc -o campiello_matter.rsrc $(MT)/Matter.rdef
	xres -o $@ campiello_matter.rsrc
	mimeset -f $@
	rm -f campiello_matter.rsrc

clean:
	rm -rf $(STAGE) campiello_matter $(PKG)
