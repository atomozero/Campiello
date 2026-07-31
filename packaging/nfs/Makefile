# Build the OPTIONAL Campiello NFS package (.hpkg).
#
# Ships the NFS export lister + its handler manifest. Pure RPC over UDP (no third-party dependency),
# so this add-on is itself MIT-clean; kept optional only because it is device-specific.
#
#   make package    # builds campiello_nfs-<version>-<arch>.hpkg
#   pkgman install ./campiello_nfs-0.1.0-1-x86_64.hpkg
#
# Haiku-only (needs the `package` tool).

VERSION := 0.1.0-2
ARCH := x86_64
PKG := campiello_nfs-$(VERSION)-$(ARCH).hpkg

CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O2

NFS := ../../optional/nfs
SRC := $(NFS)/campiello_nfs.cpp $(NFS)/NfsProbe.cpp
STAGE := stage

.PHONY: package clean
package: $(PKG)

$(PKG): campiello_nfs.PackageInfo campiello_nfs
	rm -rf $(STAGE)
	mkdir -p $(STAGE)/apps $(STAGE)/data/campiello/handlers
	cp campiello_nfs $(STAGE)/apps/campiello_nfs
	cp $(NFS)/nfs.handler $(STAGE)/data/campiello/handlers/nfs.handler
	package create -C $(STAGE) -i campiello_nfs.PackageInfo $(PKG)
	@echo "Built $(PKG)"

# The export lister, with its app_signature resource so be_roster can launch it by signature.
campiello_nfs: $(SRC) $(NFS)/NfsProbe.h $(NFS)/Nfs.rdef
	$(CXX) $(CXXFLAGS) -o $@ $(SRC) -lbe -lnetwork
	rc -o campiello_nfs.rsrc $(NFS)/Nfs.rdef
	xres -o $@ campiello_nfs.rsrc
	mimeset -f $@
	rm -f campiello_nfs.rsrc

clean:
	rm -rf $(STAGE) campiello_nfs $(PKG)
