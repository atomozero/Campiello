# Build the OPTIONAL Campiello FTP package (.hpkg).
#
# Ships the FTP browser + its handler manifest. Pure sockets (no third-party dependency), so this
# add-on is itself MIT-clean; kept optional only because it is device-specific.
#
#   make package    # builds campiello_ftp-<version>-<arch>.hpkg
#   pkgman install ./campiello_ftp-0.1.0-1-x86_64.hpkg
#
# Haiku-only (needs the `package` tool).

VERSION := 0.1.0-2
ARCH := x86_64
PKG := campiello_ftp-$(VERSION)-$(ARCH).hpkg

CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O2

FTP := ../../optional/ftp
SRC := $(FTP)/campiello_ftp.cpp $(FTP)/FtpClient.cpp
STAGE := stage

.PHONY: package clean
package: $(PKG)

$(PKG): campiello_ftp.PackageInfo campiello_ftp
	rm -rf $(STAGE)
	mkdir -p $(STAGE)/apps $(STAGE)/data/campiello/handlers
	cp campiello_ftp $(STAGE)/apps/campiello_ftp
	cp $(FTP)/ftp.handler $(STAGE)/data/campiello/handlers/ftp.handler
	package create -C $(STAGE) -i campiello_ftp.PackageInfo $(PKG)
	@echo "Built $(PKG)"

# The browser, with its app_signature resource so be_roster can launch it by signature.
# BFilePanel (the download save picker) lives in libtracker.
campiello_ftp: $(SRC) $(FTP)/FtpClient.h $(FTP)/Ftp.rdef
	$(CXX) $(CXXFLAGS) -o $@ $(SRC) -lbe -lnetwork -ltracker
	rc -o campiello_ftp.rsrc $(FTP)/Ftp.rdef
	xres -o $@ campiello_ftp.rsrc
	mimeset -f $@
	rm -f campiello_ftp.rsrc

clean:
	rm -rf $(STAGE) campiello_ftp $(PKG)
