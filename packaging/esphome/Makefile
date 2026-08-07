# Build the OPTIONAL Campiello ESPHome package (.hpkg).
#
# Ships the ESPHome info add-on + its handler manifest. No network and no third-party dependency (it
# reads the mDNS TXT and opens the web UI via be_roster), so this add-on is itself MIT-clean; kept
# optional only because it is device-specific.
#
#   make package    # builds campiello_esphome-<version>-<arch>.hpkg
#   pkgman install ./campiello_esphome-0.1.0-1-x86_64.hpkg
#
# Haiku-only (needs the `package` tool).

VERSION := 0.1.0-2
ARCH := x86_64
PKG := campiello_esphome-$(VERSION)-$(ARCH).hpkg

CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O2

ESP := ../../optional/esphome
SRC := $(ESP)/campiello_esphome.cpp
STAGE := stage

# Localization (Haiku Locale Kit). Source strings are Italian and show when no catalog matches the
# user's language; the catalogs here translate them. CATALOG_DIR is the signature with the
# "application/" prefix dropped, which is where BLocaleRoster looks (see the system catalogs).
CATALOG_SIG := application/x-vnd.Campiello-esphome
CATALOG_DIR := x-vnd.Campiello-esphome
LANGS := en
CATALOGS := $(LANGS:%=%.catalog)

.PHONY: package clean
package: $(PKG)

$(PKG): campiello_esphome.PackageInfo campiello_esphome $(CATALOGS)
	rm -rf $(STAGE)
	mkdir -p $(STAGE)/apps $(STAGE)/data/campiello/handlers
	mkdir -p $(STAGE)/data/locale/catalogs/$(CATALOG_DIR)
	cp campiello_esphome $(STAGE)/apps/campiello_esphome
	cp $(ESP)/esphome.handler $(STAGE)/data/campiello/handlers/esphome.handler
	for l in $(LANGS); do cp $$l.catalog $(STAGE)/data/locale/catalogs/$(CATALOG_DIR)/$$l.catalog; done
	package create -C $(STAGE) -i campiello_esphome.PackageInfo $(PKG)
	@echo "Built $(PKG)"

# The info app, with its app_signature resource so be_roster can launch it by signature; liblocalestub
# so B_TRANSLATE resolves the app's catalog at runtime.
campiello_esphome: $(SRC) $(ESP)/Esphome.rdef
	$(CXX) $(CXXFLAGS) -o $@ $(SRC) -lbe -llocalestub
	rc -o campiello_esphome.rsrc $(ESP)/Esphome.rdef
	xres -o $@ campiello_esphome.rsrc
	mimeset -f $@
	rm -f campiello_esphome.rsrc

# Build a catalog from its checked-in catkeys. To refresh the catkeys after changing strings, see
# `make catkeys` below.
%.catalog: $(ESP)/locales/%.catkeys
	linkcatkeys -l $* -s $(CATALOG_SIG) -o $@ $<

# Regenerate the catkeys template from the source (run after adding/changing B_TRANSLATE strings):
# preprocess in collecting mode (-P drops the line markers that break the key scanner), then collect.
.PHONY: catkeys
catkeys:
	$(CXX) -DB_COLLECTING_CATKEYS -E -P -x c++ -std=c++17 $(ESP)/campiello_esphome.cpp > esphome.pre
	collectcatkeys -s $(CATALOG_SIG) -l it -o $(ESP)/locales/it.catkeys esphome.pre
	rm -f esphome.pre
	@echo "Refreshed locales/it.catkeys; merge new keys into the other locales/*.catkeys by hand."

clean:
	rm -rf $(STAGE) campiello_esphome $(CATALOGS) esphome.pre $(PKG)
