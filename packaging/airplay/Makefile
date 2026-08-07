# Build the OPTIONAL Campiello AirPlay package (.hpkg).
#
# Ships the AirPlay info add-on + its handler manifest. No crypto, no third-party dependency, so this
# add-on is itself MIT-clean; kept optional only because it is device-specific.
#
#   make package    # builds campiello_airplay-<version>-<arch>.hpkg
#   pkgman install ./campiello_airplay-0.1.0-1-x86_64.hpkg
#
# Haiku-only (needs the `package` tool).

VERSION := 0.1.0-3
ARCH := x86_64
PKG := campiello_airplay-$(VERSION)-$(ARCH).hpkg

CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O2

AP := ../../optional/airplay
SRC := $(AP)/campiello_airplay.cpp $(AP)/AirplayInfo.cpp
STAGE := stage

# Localization (Haiku Locale Kit). Source strings are Italian and show when no catalog matches the
# user's language; the catalogs here translate them. CATALOG_DIR is the signature with the
# "application/" prefix dropped, which is where BLocaleRoster looks (see the system catalogs).
CATALOG_SIG := application/x-vnd.Campiello-airplay
CATALOG_DIR := x-vnd.Campiello-airplay
LANGS := en
CATALOGS := $(LANGS:%=%.catalog)

.PHONY: package clean
package: $(PKG)

$(PKG): campiello_airplay.PackageInfo campiello_airplay $(CATALOGS)
	rm -rf $(STAGE)
	mkdir -p $(STAGE)/apps $(STAGE)/data/campiello/handlers
	mkdir -p $(STAGE)/data/locale/catalogs/$(CATALOG_DIR)
	cp campiello_airplay $(STAGE)/apps/campiello_airplay
	cp $(AP)/airplay.handler $(STAGE)/data/campiello/handlers/airplay.handler
	for l in $(LANGS); do cp $$l.catalog $(STAGE)/data/locale/catalogs/$(CATALOG_DIR)/$$l.catalog; done
	package create -C $(STAGE) -i campiello_airplay.PackageInfo $(PKG)
	@echo "Built $(PKG)"

# The info app, with its app_signature resource so be_roster can launch it by signature; liblocalestub
# so B_TRANSLATE resolves the app's catalog at runtime.
campiello_airplay: $(SRC) $(AP)/AirplayInfo.h $(AP)/Airplay.rdef
	$(CXX) $(CXXFLAGS) -o $@ $(SRC) -lbe -llocalestub
	rc -o campiello_airplay.rsrc $(AP)/Airplay.rdef
	xres -o $@ campiello_airplay.rsrc
	mimeset -f $@
	rm -f campiello_airplay.rsrc

# Build a catalog from its checked-in catkeys. To refresh the catkeys after changing strings, see
# `make catkeys` below.
%.catalog: $(AP)/locales/%.catkeys
	linkcatkeys -l $* -s $(CATALOG_SIG) -o $@ $<

# Regenerate the catkeys template from the source (run after adding/changing B_TRANSLATE strings):
# preprocess in collecting mode (-P drops the line markers that break the key scanner), then collect.
# Both campiello_airplay.cpp and AirplayInfo.cpp carry B_TRANSLATE(_MARK) strings; collectcatkeys
# overwrites its -o target rather than appending, so both preprocessed files are concatenated into
# one input and collected in a single pass.
.PHONY: catkeys
catkeys:
	$(CXX) -DB_COLLECTING_CATKEYS -E -P -x c++ -std=c++17 $(AP)/campiello_airplay.cpp > airplay.pre
	$(CXX) -DB_COLLECTING_CATKEYS -E -P -x c++ -std=c++17 $(AP)/AirplayInfo.cpp >> airplay.pre
	collectcatkeys -s $(CATALOG_SIG) -l it -o $(AP)/locales/it.catkeys airplay.pre
	rm -f airplay.pre
	@echo "Refreshed locales/it.catkeys; merge new keys into the other locales/*.catkeys by hand."

clean:
	rm -rf $(STAGE) campiello_airplay $(CATALOGS) airplay.pre $(PKG)
