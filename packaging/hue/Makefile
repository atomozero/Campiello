# Build the OPTIONAL Campiello Philips Hue package (.hpkg).
#
# Ships the Hue control panel + its handler manifest. Kept separate from the MIT core because it
# links OpenSSL (Apache-2.0); the core campiello package stays free of it (docs/DEVICE_ADDONS.md).
#
#   make package    # builds campiello_hue-<version>-<arch>.hpkg
#   pkgman install ./campiello_hue-0.1.0-1-x86_64.hpkg
#
# Haiku-only (needs the `package` tool and OpenSSL).

VERSION := 0.1.0-2
ARCH := x86_64
PKG := campiello_hue-$(VERSION)-$(ARCH).hpkg

CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O2

HUE := ../../optional/hue
SRC := $(HUE)/campiello_hue.cpp $(HUE)/HueBridge.cpp
STAGE := stage

.PHONY: package clean
package: $(PKG)

$(PKG): campiello_hue.PackageInfo campiello_hue
	rm -rf $(STAGE)
	mkdir -p $(STAGE)/apps $(STAGE)/data/campiello/handlers
	cp campiello_hue $(STAGE)/apps/campiello_hue
	cp $(HUE)/hue.handler $(STAGE)/data/campiello/handlers/hue.handler
	package create -C $(STAGE) -i campiello_hue.PackageInfo $(PKG)
	@echo "Built $(PKG)"

# The control panel, with its app_signature resource so be_roster can launch it by signature.
campiello_hue: $(SRC) $(HUE)/HueBridge.h $(HUE)/Hue.rdef
	$(CXX) $(CXXFLAGS) -o $@ $(SRC) -lbe -lssl -lcrypto -lnetwork
	rc -o campiello_hue.rsrc $(HUE)/Hue.rdef
	xres -o $@ campiello_hue.rsrc
	mimeset -f $@
	rm -f campiello_hue.rsrc

clean:
	rm -rf $(STAGE) campiello_hue $(PKG)
