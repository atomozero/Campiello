# Build the OPTIONAL Campiello eero package (.hpkg).
#
# Ships the eero info add-on + its handler manifest. Informational only (no local control API for
# eero), no third-party dependency, so this add-on is itself MIT-clean; kept optional only because it
# is device-specific.
#
#   make package    # builds campiello_eero-<version>-<arch>.hpkg
#   pkgman install ./campiello_eero-0.1.0-1-x86_64.hpkg
#
# Haiku-only (needs the `package` tool).

VERSION := 0.1.0-2
ARCH := x86_64
PKG := campiello_eero-$(VERSION)-$(ARCH).hpkg

CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O2

EERO := ../../optional/eero
SRC := $(EERO)/campiello_eero.cpp
STAGE := stage

# Localization (Haiku Locale Kit). Source strings are Italian and show when no catalog matches the
# user's language; the catalogs here translate them. CATALOG_DIR is the signature with the
# "application/" prefix dropped, which is where BLocaleRoster looks (see the system catalogs).
CATALOG_SIG := application/x-vnd.Campiello-eero
CATALOG_DIR := x-vnd.Campiello-eero
LANGS := en
CATALOGS := $(LANGS:%=%.catalog)

.PHONY: package clean
package: $(PKG)

$(PKG): campiello_eero.PackageInfo campiello_eero $(CATALOGS)
	rm -rf $(STAGE)
	mkdir -p $(STAGE)/apps $(STAGE)/data/campiello/handlers
	mkdir -p $(STAGE)/data/locale/catalogs/$(CATALOG_DIR)
	cp campiello_eero $(STAGE)/apps/campiello_eero
	cp $(EERO)/eero.handler $(STAGE)/data/campiello/handlers/eero.handler
	for l in $(LANGS); do cp $$l.catalog $(STAGE)/data/locale/catalogs/$(CATALOG_DIR)/$$l.catalog; done
	package create -C $(STAGE) -i campiello_eero.PackageInfo $(PKG)
	@echo "Built $(PKG)"

# The info app, with its app_signature resource so be_roster can launch it by signature; liblocalestub
# so B_TRANSLATE resolves the app's catalog at runtime.
campiello_eero: $(SRC) $(EERO)/Eero.rdef
	$(CXX) $(CXXFLAGS) -o $@ $(SRC) -lbe -llocalestub
	rc -o campiello_eero.rsrc $(EERO)/Eero.rdef
	xres -o $@ campiello_eero.rsrc
	mimeset -f $@
	rm -f campiello_eero.rsrc

# Build a catalog from its checked-in catkeys. To refresh the catkeys after changing strings, see
# `make catkeys` below.
%.catalog: $(EERO)/locales/%.catkeys
	linkcatkeys -l $* -s $(CATALOG_SIG) -o $@ $<

# Regenerate the catkeys template from the source (run after adding/changing B_TRANSLATE strings):
# preprocess in collecting mode (-P drops the line markers that break the key scanner), then collect.
.PHONY: catkeys
catkeys:
	$(CXX) -DB_COLLECTING_CATKEYS -E -P -x c++ -std=c++17 $(EERO)/campiello_eero.cpp > eero.pre
	collectcatkeys -s $(CATALOG_SIG) -l it -o $(EERO)/locales/it.catkeys eero.pre
	rm -f eero.pre
	@echo "Refreshed locales/it.catkeys; merge new keys into the other locales/*.catkeys by hand."

clean:
	rm -rf $(STAGE) campiello_eero $(CATALOGS) eero.pre $(PKG)
