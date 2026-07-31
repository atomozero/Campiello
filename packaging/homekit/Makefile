# Build the OPTIONAL Campiello HomeKit package (.hpkg).
#
# Ships the HomeKit info add-on + its handler manifest. No crypto, no third-party dependency, so this
# add-on is itself MIT-clean; kept optional only because it is device-specific.
#
#   make package    # builds campiello_homekit-<version>-<arch>.hpkg
#   pkgman install ./campiello_homekit-0.1.0-1-x86_64.hpkg
#
# Haiku-only (needs the `package` tool).

VERSION := 0.1.0-3
ARCH := x86_64
PKG := campiello_homekit-$(VERSION)-$(ARCH).hpkg

CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O2

HK := ../../optional/homekit
SRC := $(HK)/campiello_homekit.cpp $(HK)/HapInfo.cpp
STAGE := stage

.PHONY: package clean
package: $(PKG)

$(PKG): campiello_homekit.PackageInfo campiello_homekit
	rm -rf $(STAGE)
	mkdir -p $(STAGE)/apps $(STAGE)/data/campiello/handlers
	cp campiello_homekit $(STAGE)/apps/campiello_homekit
	cp $(HK)/homekit.handler $(STAGE)/data/campiello/handlers/homekit.handler
	package create -C $(STAGE) -i campiello_homekit.PackageInfo $(PKG)
	@echo "Built $(PKG)"

# The info app, with its app_signature resource so be_roster can launch it by signature.
campiello_homekit: $(SRC) $(HK)/HapInfo.h $(HK)/HomeKit.rdef
	$(CXX) $(CXXFLAGS) -o $@ $(SRC) -lbe
	rc -o campiello_homekit.rsrc $(HK)/HomeKit.rdef
	xres -o $@ campiello_homekit.rsrc
	mimeset -f $@
	rm -f campiello_homekit.rsrc

clean:
	rm -rf $(STAGE) campiello_homekit $(PKG)
