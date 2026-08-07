# Build the OPTIONAL Campiello DAAP music-library package (.hpkg).
#
# Ships the DAAP library browser + its handler manifest. Pure HTTP + a hand-rolled DMAP parser (no
# third-party dependency), so this add-on is itself MIT-clean; kept optional only because it is
# device-specific.
#
#   make package    # builds campiello_daap-<version>-<arch>.hpkg
#   pkgman install ./campiello_daap-0.1.0-1-x86_64.hpkg
#
# Haiku-only (needs the `package` tool).

VERSION := 0.1.0-3
ARCH := x86_64
PKG := campiello_daap-$(VERSION)-$(ARCH).hpkg

CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O2

DAAP := ../../optional/daap
SRC := $(DAAP)/campiello_daap.cpp $(DAAP)/DaapClient.cpp
STAGE := stage

# Localization (Haiku Locale Kit). Source strings are Italian and show when no catalog matches the
# user's language; the catalogs here translate them. CATALOG_DIR is the signature with the
# "application/" prefix dropped, which is where BLocaleRoster looks (see the system catalogs).
CATALOG_SIG := application/x-vnd.Campiello-daap
CATALOG_DIR := x-vnd.Campiello-daap
LANGS := en
CATALOGS := $(LANGS:%=%.catalog)

.PHONY: package clean
package: $(PKG)

$(PKG): campiello_daap.PackageInfo campiello_daap $(CATALOGS)
	rm -rf $(STAGE)
	mkdir -p $(STAGE)/apps $(STAGE)/data/campiello/handlers
	mkdir -p $(STAGE)/data/locale/catalogs/$(CATALOG_DIR)
	cp campiello_daap $(STAGE)/apps/campiello_daap
	cp $(DAAP)/daap.handler $(STAGE)/data/campiello/handlers/daap.handler
	for l in $(LANGS); do cp $$l.catalog $(STAGE)/data/locale/catalogs/$(CATALOG_DIR)/$$l.catalog; done
	package create -C $(STAGE) -i campiello_daap.PackageInfo $(PKG)
	@echo "Built $(PKG)"

# The library browser, with its app_signature resource so be_roster can launch it by signature;
# liblocalestub so B_TRANSLATE resolves the app's catalog at runtime.
campiello_daap: $(SRC) $(DAAP)/DaapClient.h $(DAAP)/Daap.rdef
	$(CXX) $(CXXFLAGS) -o $@ $(SRC) -lbe -lnetwork -llocalestub
	rc -o campiello_daap.rsrc $(DAAP)/Daap.rdef
	xres -o $@ campiello_daap.rsrc
	mimeset -f $@
	rm -f campiello_daap.rsrc

# Build a catalog from its checked-in catkeys. To refresh the catkeys after changing strings, see
# `make catkeys` below.
%.catalog: $(DAAP)/locales/%.catkeys
	linkcatkeys -l $* -s $(CATALOG_SIG) -o $@ $<

# Regenerate the catkeys template from the source (run after adding/changing B_TRANSLATE strings):
# preprocess in collecting mode (-P drops the line markers that break the key scanner), then collect.
.PHONY: catkeys
catkeys:
	$(CXX) -DB_COLLECTING_CATKEYS -E -P -x c++ -std=c++17 $(DAAP)/campiello_daap.cpp > daap.pre
	collectcatkeys -s $(CATALOG_SIG) -l it -o $(DAAP)/locales/it.catkeys daap.pre
	rm -f daap.pre
	@echo "Refreshed locales/it.catkeys; merge new keys into the other locales/*.catkeys by hand."

clean:
	rm -rf $(STAGE) campiello_daap $(CATALOGS) daap.pre $(PKG)
