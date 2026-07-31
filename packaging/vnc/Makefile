# Build the OPTIONAL Campiello VNC remote-desktop package (.hpkg).
#
# Ships the VNC launcher + its handler manifest. No third-party dependency (it hands off to an
# installed viewer), so this add-on is itself MIT-clean; kept optional only because it is
# device-specific.
#
#   make package    # builds campiello_vnc-<version>-<arch>.hpkg
#   pkgman install ./campiello_vnc-0.1.0-1-x86_64.hpkg
#
# Haiku-only (needs the `package` tool).

VERSION := 0.1.0-2
ARCH := x86_64
PKG := campiello_vnc-$(VERSION)-$(ARCH).hpkg

CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O2

VNC := ../../optional/vnc
SRC := $(VNC)/campiello_vnc.cpp
STAGE := stage

.PHONY: package clean
package: $(PKG)

$(PKG): campiello_vnc.PackageInfo campiello_vnc
	rm -rf $(STAGE)
	mkdir -p $(STAGE)/apps $(STAGE)/data/campiello/handlers
	cp campiello_vnc $(STAGE)/apps/campiello_vnc
	cp $(VNC)/vnc.handler $(STAGE)/data/campiello/handlers/vnc.handler
	package create -C $(STAGE) -i campiello_vnc.PackageInfo $(PKG)
	@echo "Built $(PKG)"

# The launcher, with its app_signature resource so be_roster can launch it by signature.
campiello_vnc: $(SRC) $(VNC)/Vnc.rdef
	$(CXX) $(CXXFLAGS) -o $@ $(SRC) -lbe
	rc -o campiello_vnc.rsrc $(VNC)/Vnc.rdef
	xres -o $@ campiello_vnc.rsrc
	mimeset -f $@
	rm -f campiello_vnc.rsrc

clean:
	rm -rf $(STAGE) campiello_vnc $(PKG)
