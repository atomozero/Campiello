# Build the OPTIONAL Campiello eSCL scanning package (.hpkg).
#
# Ships the eSCL scanner panel + its handler manifest. Pure HTTP/XML (no third-party dependency), so
# this add-on is itself MIT-clean; kept optional only because it is device-specific.
#
#   make package    # builds campiello_escl-<version>-<arch>.hpkg
#   pkgman install ./campiello_escl-0.1.0-1-x86_64.hpkg
#
# Haiku-only (needs the `package` tool).

VERSION := 0.1.0-2
ARCH := x86_64
PKG := campiello_escl-$(VERSION)-$(ARCH).hpkg

CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O2

ESCL := ../../optional/escl
SRC := $(ESCL)/campiello_escl.cpp $(ESCL)/EsclClient.cpp
STAGE := stage

.PHONY: package clean
package: $(PKG)

$(PKG): campiello_escl.PackageInfo campiello_escl
	rm -rf $(STAGE)
	mkdir -p $(STAGE)/apps $(STAGE)/data/campiello/handlers
	cp campiello_escl $(STAGE)/apps/campiello_escl
	cp $(ESCL)/escl.handler $(STAGE)/data/campiello/handlers/escl.handler
	package create -C $(STAGE) -i campiello_escl.PackageInfo $(PKG)
	@echo "Built $(PKG)"

# The scanner panel, with its app_signature resource so be_roster can launch it by signature.
# BFilePanel (the save picker) lives in libtracker.
campiello_escl: $(SRC) $(ESCL)/EsclClient.h $(ESCL)/Escl.rdef
	$(CXX) $(CXXFLAGS) -o $@ $(SRC) -lbe -lnetwork -ltracker
	rc -o campiello_escl.rsrc $(ESCL)/Escl.rdef
	xres -o $@ campiello_escl.rsrc
	mimeset -f $@
	rm -f campiello_escl.rsrc

clean:
	rm -rf $(STAGE) campiello_escl $(PKG)
