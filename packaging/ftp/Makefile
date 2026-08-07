# Build the OPTIONAL Campiello FTP package (.hpkg).
#
# Ships the FTP browser + its handler manifest. Pure sockets (no third-party dependency), so this
# add-on is itself MIT-clean; kept optional only because it is device-specific.
#
#   make package    # builds campiello_ftp-<version>-<arch>.hpkg
#   pkgman install ./campiello_ftp-0.1.0-1-x86_64.hpkg
#
# Haiku-only (needs the `package` tool).

VERSION := 0.1.0-3
ARCH := x86_64
PKG := campiello_ftp-$(VERSION)-$(ARCH).hpkg

CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O2

FTP := ../../optional/ftp
SRC := $(FTP)/campiello_ftp.cpp $(FTP)/FtpClient.cpp
STAGE := stage

# Localization (Haiku Locale Kit). Source strings are Italian and show when no catalog matches the
# user's language; the catalogs here translate them. CATALOG_DIR is the signature with the
# "application/" prefix dropped, which is where BLocaleRoster looks (see the system catalogs).
CATALOG_SIG := application/x-vnd.Campiello-ftp
CATALOG_DIR := x-vnd.Campiello-ftp
LANGS := en
CATALOGS := $(LANGS:%=%.catalog)

.PHONY: package clean
package: $(PKG)

$(PKG): campiello_ftp.PackageInfo campiello_ftp $(CATALOGS)
	rm -rf $(STAGE)
	mkdir -p $(STAGE)/apps $(STAGE)/data/campiello/handlers
	mkdir -p $(STAGE)/data/locale/catalogs/$(CATALOG_DIR)
	cp campiello_ftp $(STAGE)/apps/campiello_ftp
	cp $(FTP)/ftp.handler $(STAGE)/data/campiello/handlers/ftp.handler
	for l in $(LANGS); do cp $$l.catalog $(STAGE)/data/locale/catalogs/$(CATALOG_DIR)/$$l.catalog; done
	package create -C $(STAGE) -i campiello_ftp.PackageInfo $(PKG)
	@echo "Built $(PKG)"

# The browser, with its app_signature resource so be_roster can launch it by signature.
# BFilePanel (the download save picker) lives in libtracker; liblocalestub so B_TRANSLATE resolves
# the app's catalog at runtime.
campiello_ftp: $(SRC) $(FTP)/FtpClient.h $(FTP)/Ftp.rdef
	$(CXX) $(CXXFLAGS) -o $@ $(SRC) -lbe -lnetwork -ltracker -llocalestub
	rc -o campiello_ftp.rsrc $(FTP)/Ftp.rdef
	xres -o $@ campiello_ftp.rsrc
	mimeset -f $@
	rm -f campiello_ftp.rsrc

# Build a catalog from its checked-in catkeys. To refresh the catkeys after changing strings, see
# `make catkeys` below.
%.catalog: $(FTP)/locales/%.catkeys
	linkcatkeys -l $* -s $(CATALOG_SIG) -o $@ $<

# Regenerate the catkeys template from the source (run after adding/changing B_TRANSLATE strings):
# preprocess in collecting mode (-P drops the line markers that break the key scanner), then collect.
.PHONY: catkeys
catkeys:
	$(CXX) -DB_COLLECTING_CATKEYS -E -P -x c++ -std=c++17 $(FTP)/campiello_ftp.cpp > ftp.pre
	$(CXX) -DB_COLLECTING_CATKEYS -E -P -x c++ -std=c++17 $(FTP)/FtpClient.cpp >> ftp.pre
	collectcatkeys -s $(CATALOG_SIG) -l it -o $(FTP)/locales/it.catkeys ftp.pre
	rm -f ftp.pre
	@echo "Refreshed locales/it.catkeys; merge new keys into the other locales/*.catkeys by hand."

clean:
	rm -rf $(STAGE) campiello_ftp $(CATALOGS) ftp.pre $(PKG)
