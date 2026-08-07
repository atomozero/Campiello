# Build the OPTIONAL Campiello Lutron package (.hpkg).
#
# Ships the Lutron info add-on + its handler manifest. Informational only (control needs Secure LEAP
# TLS pairing), no third-party dependency, so this add-on is itself MIT-clean; kept optional only
# because it is device-specific.
#
#   make package    # builds campiello_lutron-<version>-<arch>.hpkg
#   pkgman install ./campiello_lutron-0.1.0-1-x86_64.hpkg
#
# Haiku-only (needs the `package` tool).

VERSION := 0.1.0-3
ARCH := x86_64
PKG := campiello_lutron-$(VERSION)-$(ARCH).hpkg

CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O2

LUT := ../../optional/lutron
SRC := $(LUT)/campiello_lutron.cpp
STAGE := stage

# Localization (Haiku Locale Kit). Source strings are Italian and show when no catalog matches the
# user's language; the catalogs here translate them. CATALOG_DIR is the signature with the
# "application/" prefix dropped, which is where BLocaleRoster looks (see the system catalogs).
CATALOG_SIG := application/x-vnd.Campiello-lutron
CATALOG_DIR := x-vnd.Campiello-lutron
LANGS := en
CATALOGS := $(LANGS:%=%.catalog)

.PHONY: package clean
package: $(PKG)

$(PKG): campiello_lutron.PackageInfo campiello_lutron $(CATALOGS)
	rm -rf $(STAGE)
	mkdir -p $(STAGE)/apps $(STAGE)/data/campiello/handlers
	mkdir -p $(STAGE)/data/locale/catalogs/$(CATALOG_DIR)
	cp campiello_lutron $(STAGE)/apps/campiello_lutron
	cp $(LUT)/lutron.handler $(STAGE)/data/campiello/handlers/lutron.handler
	for l in $(LANGS); do cp $$l.catalog $(STAGE)/data/locale/catalogs/$(CATALOG_DIR)/$$l.catalog; done
	package create -C $(STAGE) -i campiello_lutron.PackageInfo $(PKG)
	@echo "Built $(PKG)"

# The info app, with its app_signature resource so be_roster can launch it by signature; liblocalestub
# so B_TRANSLATE resolves the app's catalog at runtime.
campiello_lutron: $(SRC) $(LUT)/Lutron.rdef
	$(CXX) $(CXXFLAGS) -o $@ $(SRC) -lbe -llocalestub
	rc -o campiello_lutron.rsrc $(LUT)/Lutron.rdef
	xres -o $@ campiello_lutron.rsrc
	mimeset -f $@
	rm -f campiello_lutron.rsrc

# Build a catalog from its checked-in catkeys. To refresh the catkeys after changing strings, see
# `make catkeys` below.
%.catalog: $(LUT)/locales/%.catkeys
	linkcatkeys -l $* -s $(CATALOG_SIG) -o $@ $<

# Regenerate the catkeys template from the source (run after adding/changing B_TRANSLATE strings):
# preprocess in collecting mode (-P drops the line markers that break the key scanner), then collect.
.PHONY: catkeys
catkeys:
	$(CXX) -DB_COLLECTING_CATKEYS -E -P -x c++ -std=c++17 $(LUT)/campiello_lutron.cpp > lutron.pre
	collectcatkeys -s $(CATALOG_SIG) -l it -o $(LUT)/locales/it.catkeys lutron.pre
	rm -f lutron.pre
	@echo "Refreshed locales/it.catkeys; merge new keys into the other locales/*.catkeys by hand."

clean:
	rm -rf $(STAGE) campiello_lutron $(CATALOGS) lutron.pre $(PKG)
