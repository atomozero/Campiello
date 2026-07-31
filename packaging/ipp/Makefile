# Build the OPTIONAL Campiello IPP printing package (.hpkg).
#
# Ships the IPP printer panel + its handler manifest. Pure IPP (no third-party dependency), so this
# add-on is itself MIT-clean; it is kept optional only because it is device-specific.
#
#   make package    # builds campiello_ipp-<version>-<arch>.hpkg
#   pkgman install ./campiello_ipp-0.1.0-1-x86_64.hpkg
#
# Haiku-only (needs the `package` tool).

VERSION := 0.1.0-2
ARCH := x86_64
PKG := campiello_ipp-$(VERSION)-$(ARCH).hpkg

CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O2

IPP := ../../optional/ipp
SRC := $(IPP)/campiello_ipp.cpp $(IPP)/IppClient.cpp
STAGE := stage

.PHONY: package clean
package: $(PKG)

$(PKG): campiello_ipp.PackageInfo campiello_ipp
	rm -rf $(STAGE)
	mkdir -p $(STAGE)/apps $(STAGE)/data/campiello/handlers
	cp campiello_ipp $(STAGE)/apps/campiello_ipp
	cp $(IPP)/ipp.handler $(STAGE)/data/campiello/handlers/ipp.handler
	package create -C $(STAGE) -i campiello_ipp.PackageInfo $(PKG)
	@echo "Built $(PKG)"

# The printer panel, with its app_signature resource so be_roster can launch it by signature.
# BFilePanel (the "Stampa un file..." picker) lives in libtracker.
campiello_ipp: $(SRC) $(IPP)/IppClient.h $(IPP)/Ipp.rdef
	$(CXX) $(CXXFLAGS) -o $@ $(SRC) -lbe -lnetwork -ltracker
	rc -o campiello_ipp.rsrc $(IPP)/Ipp.rdef
	xres -o $@ campiello_ipp.rsrc
	mimeset -f $@
	rm -f campiello_ipp.rsrc

clean:
	rm -rf $(STAGE) campiello_ipp $(PKG)
