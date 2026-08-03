# Build the OPTIONAL Campiello Daikin package (.hpkg).
#
# Ships the Daikin control panel + its handler manifest. Pure HTTP over the adapter's open local API
# (no third-party dependency, no cloud, no crypto), so this add-on is itself MIT-clean; kept optional
# only because it is device-specific.
#
#   make package    # builds campiello_daikin-<version>-<arch>.hpkg
#   pkgman install ./campiello_daikin-0.1.0-1-x86_64.hpkg
#
# Haiku-only (needs the `package` tool).

VERSION := 0.1.0-1
ARCH := x86_64
PKG := campiello_daikin-$(VERSION)-$(ARCH).hpkg

CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O2

DK := ../../optional/daikin
SRC := $(DK)/campiello_daikin.cpp $(DK)/DaikinClient.cpp
STAGE := stage

.PHONY: package clean test
package: $(PKG)

$(PKG): campiello_daikin.PackageInfo campiello_daikin
	rm -rf $(STAGE)
	mkdir -p $(STAGE)/apps $(STAGE)/data/campiello/handlers
	cp campiello_daikin $(STAGE)/apps/campiello_daikin
	cp $(DK)/daikin.handler $(STAGE)/data/campiello/handlers/daikin.handler
	package create -C $(STAGE) -i campiello_daikin.PackageInfo $(PKG)
	@echo "Built $(PKG)"

# The control panel, with its app_signature resource so be_roster can launch it by signature.
campiello_daikin: $(SRC) $(DK)/DaikinClient.h $(DK)/Daikin.rdef
	$(CXX) $(CXXFLAGS) -o $@ $(SRC) -lbe -lnetwork
	rc -o campiello_daikin.rsrc $(DK)/Daikin.rdef
	xres -o $@ campiello_daikin.rsrc
	mimeset -f $@
	rm -f campiello_daikin.rsrc

# Protocol parser unit test (no network).
test:
	$(CXX) $(CXXFLAGS) -o test_daikin $(DK)/test_daikin.cpp $(DK)/DaikinClient.cpp -lnetwork
	./test_daikin

clean:
	rm -rf $(STAGE) campiello_daikin test_daikin $(PKG)
