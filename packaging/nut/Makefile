# Build the OPTIONAL Campiello NUT (UPS) package (.hpkg).
#
# Ships the UPS monitor + its handler manifest. Pure sockets over the NUT text protocol (no
# third-party dependency), read-only, so this add-on is itself MIT-clean; kept optional only because
# it is device-specific.
#
#   make package    # builds campiello_nut-<version>-<arch>.hpkg
#   pkgman install ./campiello_nut-0.1.0-1-x86_64.hpkg
#
# Haiku-only (needs the `package` tool).

VERSION := 0.1.0-1
ARCH := x86_64
PKG := campiello_nut-$(VERSION)-$(ARCH).hpkg

CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O2

NUT := ../../optional/nut
SRC := $(NUT)/campiello_nut.cpp $(NUT)/NutClient.cpp
STAGE := stage

.PHONY: package clean test
package: $(PKG)

$(PKG): campiello_nut.PackageInfo campiello_nut
	rm -rf $(STAGE)
	mkdir -p $(STAGE)/apps $(STAGE)/data/campiello/handlers
	cp campiello_nut $(STAGE)/apps/campiello_nut
	cp $(NUT)/nut.handler $(STAGE)/data/campiello/handlers/nut.handler
	package create -C $(STAGE) -i campiello_nut.PackageInfo $(PKG)
	@echo "Built $(PKG)"

# The UPS monitor, with its app_signature resource so be_roster can launch it by signature.
campiello_nut: $(SRC) $(NUT)/NutClient.h $(NUT)/Nut.rdef
	$(CXX) $(CXXFLAGS) -o $@ $(SRC) -lbe -lnetwork
	rc -o campiello_nut.rsrc $(NUT)/Nut.rdef
	xres -o $@ campiello_nut.rsrc
	mimeset -f $@
	rm -f campiello_nut.rsrc

# Protocol parser unit test (no network).
test:
	$(CXX) $(CXXFLAGS) -o test_nut $(NUT)/test_nut.cpp $(NUT)/NutClient.cpp -lnetwork
	./test_nut

clean:
	rm -rf $(STAGE) campiello_nut test_nut $(PKG)
