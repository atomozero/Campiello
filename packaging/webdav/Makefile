# Build the OPTIONAL Campiello WebDAV package (.hpkg).
#
# Ships the WebDAV browser + its handler manifest. Pure HTTP/XML (no third-party dependency), so this
# add-on is itself MIT-clean; kept optional only because it is device-specific.
#
#   make package    # builds campiello_webdav-<version>-<arch>.hpkg
#   pkgman install ./campiello_webdav-0.1.0-1-x86_64.hpkg
#
# Haiku-only (needs the `package` tool).

VERSION := 0.1.0-3
ARCH := x86_64
PKG := campiello_webdav-$(VERSION)-$(ARCH).hpkg

CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O2

DAV := ../../optional/webdav
SRC := $(DAV)/campiello_webdav.cpp $(DAV)/WebDavClient.cpp
STAGE := stage

# Localization (Haiku Locale Kit). Source strings are Italian and show when no catalog matches the
# user's language; the catalogs here translate them. CATALOG_DIR is the signature with the
# "application/" prefix dropped, which is where BLocaleRoster looks (see the system catalogs).
CATALOG_SIG := application/x-vnd.Campiello-webdav
CATALOG_DIR := x-vnd.Campiello-webdav
LANGS := en
CATALOGS := $(LANGS:%=%.catalog)

.PHONY: package clean
package: $(PKG)

$(PKG): campiello_webdav.PackageInfo campiello_webdav $(CATALOGS)
	rm -rf $(STAGE)
	mkdir -p $(STAGE)/apps $(STAGE)/data/campiello/handlers
	mkdir -p $(STAGE)/data/locale/catalogs/$(CATALOG_DIR)
	cp campiello_webdav $(STAGE)/apps/campiello_webdav
	cp $(DAV)/webdav.handler $(STAGE)/data/campiello/handlers/webdav.handler
	for l in $(LANGS); do cp $$l.catalog $(STAGE)/data/locale/catalogs/$(CATALOG_DIR)/$$l.catalog; done
	package create -C $(STAGE) -i campiello_webdav.PackageInfo $(PKG)
	@echo "Built $(PKG)"

# The browser, with its app_signature resource so be_roster can launch it by signature.
# BFilePanel (the download save picker) lives in libtracker; liblocalestub so B_TRANSLATE resolves
# the app's catalog at runtime.
campiello_webdav: $(SRC) $(DAV)/WebDavClient.h $(DAV)/Webdav.rdef
	$(CXX) $(CXXFLAGS) -o $@ $(SRC) -lbe -lnetwork -ltracker -llocalestub
	rc -o campiello_webdav.rsrc $(DAV)/Webdav.rdef
	xres -o $@ campiello_webdav.rsrc
	mimeset -f $@
	rm -f campiello_webdav.rsrc

# Build a catalog from its checked-in catkeys. To refresh the catkeys after changing strings, see
# `make catkeys` below.
%.catalog: $(DAV)/locales/%.catkeys
	linkcatkeys -l $* -s $(CATALOG_SIG) -o $@ $<

# Regenerate the catkeys template from the source (run after adding/changing B_TRANSLATE strings):
# preprocess in collecting mode (-P drops the line markers that break the key scanner), then collect.
.PHONY: catkeys
catkeys:
	$(CXX) -DB_COLLECTING_CATKEYS -E -P -x c++ -std=c++17 $(DAV)/campiello_webdav.cpp > webdav.pre
	collectcatkeys -s $(CATALOG_SIG) -l it -o $(DAV)/locales/it.catkeys webdav.pre
	rm -f webdav.pre
	@echo "Refreshed locales/it.catkeys; merge new keys into the other locales/*.catkeys by hand."

clean:
	rm -rf $(STAGE) campiello_webdav $(CATALOGS) webdav.pre $(PKG)
