# Build the OPTIONAL Campiello Alexa package (.hpkg).
#
# Ships the Alexa info add-on + its handler manifest. Informational only (no local control API for
# Alexa), no third-party dependency, so this add-on is itself MIT-clean; kept optional only because it
# is device-specific.
#
#   make package    # builds campiello_alexa-<version>-<arch>.hpkg
#   pkgman install ./campiello_alexa-0.1.0-1-x86_64.hpkg
#
# Haiku-only (needs the `package` tool).

VERSION := 0.1.0-2
ARCH := x86_64
PKG := campiello_alexa-$(VERSION)-$(ARCH).hpkg

CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O2

ALX := ../../optional/alexa
SRC := $(ALX)/campiello_alexa.cpp
STAGE := stage

.PHONY: package clean
package: $(PKG)

$(PKG): campiello_alexa.PackageInfo campiello_alexa
	rm -rf $(STAGE)
	mkdir -p $(STAGE)/apps $(STAGE)/data/campiello/handlers
	cp campiello_alexa $(STAGE)/apps/campiello_alexa
	cp $(ALX)/alexa.handler $(STAGE)/data/campiello/handlers/alexa.handler
	package create -C $(STAGE) -i campiello_alexa.PackageInfo $(PKG)
	@echo "Built $(PKG)"

# The info app, with its app_signature resource so be_roster can launch it by signature.
campiello_alexa: $(SRC) $(ALX)/Alexa.rdef
	$(CXX) $(CXXFLAGS) -o $@ $(SRC) -lbe
	rc -o campiello_alexa.rsrc $(ALX)/Alexa.rdef
	xres -o $@ campiello_alexa.rsrc
	mimeset -f $@
	rm -f campiello_alexa.rsrc

clean:
	rm -rf $(STAGE) campiello_alexa $(PKG)
