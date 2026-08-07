# Build the OPTIONAL Campiello Alexa package (.hpkg).
#
# Ships the Alexa info add-on + its handler manifest. Informational only (no local control API for
# Alexa), no third-party dependency, so this add-on is itself MIT-clean; kept optional only because it
# is device-specific.
#
#   make package    # builds campiello_alexa-<version>-<arch>.hpkg
#   pkgman install ./campiello_alexa-0.1.0-1-x86_64.hpkg
#
# Haiku-only (needs the `package` tool).

VERSION := 0.1.0-3
ARCH := x86_64
PKG := campiello_alexa-$(VERSION)-$(ARCH).hpkg

CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O2

ALX := ../../optional/alexa
SRC := $(ALX)/campiello_alexa.cpp
STAGE := stage

# Localization (Haiku Locale Kit). Source strings are Italian and show when no catalog matches the
# user's language; the catalogs here translate them. CATALOG_DIR is the signature with the
# "application/" prefix dropped, which is where BLocaleRoster looks (see the system catalogs).
CATALOG_SIG := application/x-vnd.Campiello-alexa
CATALOG_DIR := x-vnd.Campiello-alexa
LANGS := en
CATALOGS := $(LANGS:%=%.catalog)

.PHONY: package clean
package: $(PKG)

$(PKG): campiello_alexa.PackageInfo campiello_alexa $(CATALOGS)
	rm -rf $(STAGE)
	mkdir -p $(STAGE)/apps $(STAGE)/data/campiello/handlers
	mkdir -p $(STAGE)/data/locale/catalogs/$(CATALOG_DIR)
	cp campiello_alexa $(STAGE)/apps/campiello_alexa
	cp $(ALX)/alexa.handler $(STAGE)/data/campiello/handlers/alexa.handler
	for l in $(LANGS); do cp $$l.catalog $(STAGE)/data/locale/catalogs/$(CATALOG_DIR)/$$l.catalog; done
	package create -C $(STAGE) -i campiello_alexa.PackageInfo $(PKG)
	@echo "Built $(PKG)"

# The info app, with its app_signature resource so be_roster can launch it by signature; liblocalestub
# so B_TRANSLATE resolves the app's catalog at runtime.
campiello_alexa: $(SRC) $(ALX)/Alexa.rdef
	$(CXX) $(CXXFLAGS) -o $@ $(SRC) -lbe -llocalestub
	rc -o campiello_alexa.rsrc $(ALX)/Alexa.rdef
	xres -o $@ campiello_alexa.rsrc
	mimeset -f $@
	rm -f campiello_alexa.rsrc

# Build a catalog from its checked-in catkeys. To refresh the catkeys after changing strings, see
# `make catkeys` below.
%.catalog: $(ALX)/locales/%.catkeys
	linkcatkeys -l $* -s $(CATALOG_SIG) -o $@ $<

# Regenerate the catkeys template from the source (run after adding/changing B_TRANSLATE strings):
# preprocess in collecting mode (-P drops the line markers that break the key scanner), then collect.
.PHONY: catkeys
catkeys:
	$(CXX) -DB_COLLECTING_CATKEYS -E -P -x c++ -std=c++17 $(ALX)/campiello_alexa.cpp > alexa.pre
	collectcatkeys -s $(CATALOG_SIG) -l it -o $(ALX)/locales/it.catkeys alexa.pre
	rm -f alexa.pre
	@echo "Refreshed locales/it.catkeys; merge new keys into the other locales/*.catkeys by hand."

clean:
	rm -rf $(STAGE) campiello_alexa $(CATALOGS) alexa.pre $(PKG)
