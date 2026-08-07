# Build the OPTIONAL Campiello Daikin package (.hpkg).
#
# Ships the Daikin control panel + its handler manifest. Pure HTTP over the adapter's open local API
# (no third-party dependency, no cloud, no crypto), so this add-on is itself MIT-clean; kept optional
# only because it is device-specific.
#
#   make package    # builds campiello_daikin-<version>-<arch>.hpkg
#   pkgman install ./campiello_daikin-0.1.0-1-x86_64.hpkg
#
# Haiku-only (needs the `package` tool).

VERSION := 0.1.0-2
ARCH := x86_64
PKG := campiello_daikin-$(VERSION)-$(ARCH).hpkg

CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O2

DK := ../../optional/daikin
SRC := $(DK)/campiello_daikin.cpp $(DK)/DaikinClient.cpp
STAGE := stage

# Localization (Haiku Locale Kit). Source strings are Italian and show when no catalog matches the
# user's language; the catalogs here translate them. CATALOG_DIR is the signature with the
# "application/" prefix dropped, which is where BLocaleRoster looks (see the system catalogs).
CATALOG_SIG := application/x-vnd.Campiello-daikin
CATALOG_DIR := x-vnd.Campiello-daikin
LANGS := en
CATALOGS := $(LANGS:%=%.catalog)

.PHONY: package clean test
package: $(PKG)

$(PKG): campiello_daikin.PackageInfo campiello_daikin $(CATALOGS)
	rm -rf $(STAGE)
	mkdir -p $(STAGE)/apps $(STAGE)/data/campiello/handlers
	mkdir -p $(STAGE)/data/locale/catalogs/$(CATALOG_DIR)
	cp campiello_daikin $(STAGE)/apps/campiello_daikin
	cp $(DK)/daikin.handler $(STAGE)/data/campiello/handlers/daikin.handler
	for l in $(LANGS); do cp $$l.catalog $(STAGE)/data/locale/catalogs/$(CATALOG_DIR)/$$l.catalog; done
	package create -C $(STAGE) -i campiello_daikin.PackageInfo $(PKG)
	@echo "Built $(PKG)"

# The control panel, with its app_signature resource so be_roster can launch it by signature;
# liblocalestub so B_TRANSLATE resolves the app's catalog at runtime.
campiello_daikin: $(SRC) $(DK)/DaikinClient.h $(DK)/Daikin.rdef
	$(CXX) $(CXXFLAGS) -o $@ $(SRC) -lbe -lnetwork -llocalestub
	rc -o campiello_daikin.rsrc $(DK)/Daikin.rdef
	xres -o $@ campiello_daikin.rsrc
	mimeset -f $@
	rm -f campiello_daikin.rsrc

# Build a catalog from its checked-in catkeys. To refresh the catkeys after changing strings, see
# `make catkeys` below.
%.catalog: $(DK)/locales/%.catkeys
	linkcatkeys -l $* -s $(CATALOG_SIG) -o $@ $<

# Regenerate the catkeys template from the source (run after adding/changing B_TRANSLATE strings):
# preprocess each translated file in collecting mode (-P drops the line markers that break the key
# scanner), concatenate, then collect once so both files land in the same it.catkeys.
.PHONY: catkeys
catkeys:
	$(CXX) -DB_COLLECTING_CATKEYS -E -P -x c++ -std=c++17 $(DK)/campiello_daikin.cpp > daikin.pre
	$(CXX) -DB_COLLECTING_CATKEYS -E -P -x c++ -std=c++17 $(DK)/DaikinClient.cpp >> daikin.pre
	collectcatkeys -s $(CATALOG_SIG) -l it -o $(DK)/locales/it.catkeys daikin.pre
	rm -f daikin.pre
	@echo "Refreshed locales/it.catkeys; merge new keys into the other locales/*.catkeys by hand."

# Protocol parser + UI-helper unit test (no network); DaikinClient.cpp's ModeName/FanRateName/
# FanDirName now go through B_TRANSLATE, so the test also needs liblocalestub at link time.
test:
	$(CXX) $(CXXFLAGS) -o test_daikin $(DK)/test_daikin.cpp $(DK)/DaikinClient.cpp -lbe -lnetwork -llocalestub
	./test_daikin

clean:
	rm -rf $(STAGE) campiello_daikin test_daikin $(CATALOGS) daikin.pre $(PKG)
