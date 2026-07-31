# Build the OPTIONAL Campiello WebDAV package (.hpkg).
#
# Ships the WebDAV browser + its handler manifest. Pure HTTP/XML (no third-party dependency), so this
# add-on is itself MIT-clean; kept optional only because it is device-specific.
#
#   make package    # builds campiello_webdav-<version>-<arch>.hpkg
#   pkgman install ./campiello_webdav-0.1.0-1-x86_64.hpkg
#
# Haiku-only (needs the `package` tool).

VERSION := 0.1.0-2
ARCH := x86_64
PKG := campiello_webdav-$(VERSION)-$(ARCH).hpkg

CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O2

DAV := ../../optional/webdav
SRC := $(DAV)/campiello_webdav.cpp $(DAV)/WebDavClient.cpp
STAGE := stage

.PHONY: package clean
package: $(PKG)

$(PKG): campiello_webdav.PackageInfo campiello_webdav
	rm -rf $(STAGE)
	mkdir -p $(STAGE)/apps $(STAGE)/data/campiello/handlers
	cp campiello_webdav $(STAGE)/apps/campiello_webdav
	cp $(DAV)/webdav.handler $(STAGE)/data/campiello/handlers/webdav.handler
	package create -C $(STAGE) -i campiello_webdav.PackageInfo $(PKG)
	@echo "Built $(PKG)"

# The browser, with its app_signature resource so be_roster can launch it by signature.
# BFilePanel (the download save picker) lives in libtracker.
campiello_webdav: $(SRC) $(DAV)/WebDavClient.h $(DAV)/Webdav.rdef
	$(CXX) $(CXXFLAGS) -o $@ $(SRC) -lbe -lnetwork -ltracker
	rc -o campiello_webdav.rsrc $(DAV)/Webdav.rdef
	xres -o $@ campiello_webdav.rsrc
	mimeset -f $@
	rm -f campiello_webdav.rsrc

clean:
	rm -rf $(STAGE) campiello_webdav $(PKG)
