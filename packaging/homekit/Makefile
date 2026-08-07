# Build the OPTIONAL Campiello HomeKit package (.hpkg).
#
# Ships the HomeKit info add-on + its handler manifest. No crypto, no third-party dependency, so this
# add-on is itself MIT-clean; kept optional only because it is device-specific.
#
#   make package    # builds campiello_homekit-<version>-<arch>.hpkg
#   pkgman install ./campiello_homekit-0.1.0-1-x86_64.hpkg
#
# Haiku-only (needs the `package` tool).

VERSION := 0.1.0-4
ARCH := x86_64
PKG := campiello_homekit-$(VERSION)-$(ARCH).hpkg

CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O2

HK := ../../optional/homekit
SRC := $(HK)/campiello_homekit.cpp $(HK)/HapInfo.cpp
STAGE := stage

# Localization (Haiku Locale Kit). Source strings are Italian and show when no catalog matches the
# user's language; the catalogs here translate them. CATALOG_DIR is the signature with the
# "application/" prefix dropped, which is where BLocaleRoster looks (see the system catalogs).
CATALOG_SIG := application/x-vnd.Campiello-homekit
CATALOG_DIR := x-vnd.Campiello-homekit
LANGS := en
CATALOGS := $(LANGS:%=%.catalog)

.PHONY: package clean
package: $(PKG)

$(PKG): campiello_homekit.PackageInfo campiello_homekit $(CATALOGS)
	rm -rf $(STAGE)
	mkdir -p $(STAGE)/apps $(STAGE)/data/campiello/handlers
	mkdir -p $(STAGE)/data/locale/catalogs/$(CATALOG_DIR)
	cp campiello_homekit $(STAGE)/apps/campiello_homekit
	cp $(HK)/homekit.handler $(STAGE)/data/campiello/handlers/homekit.handler
	for l in $(LANGS); do cp $$l.catalog $(STAGE)/data/locale/catalogs/$(CATALOG_DIR)/$$l.catalog; done
	package create -C $(STAGE) -i campiello_homekit.PackageInfo $(PKG)
	@echo "Built $(PKG)"

# The info app, with its app_signature resource so be_roster can launch it by signature; liblocalestub
# so B_TRANSLATE resolves the app's catalog at runtime.
campiello_homekit: $(SRC) $(HK)/HapInfo.h $(HK)/HomeKit.rdef
	$(CXX) $(CXXFLAGS) -o $@ $(SRC) -lbe -llocalestub
	rc -o campiello_homekit.rsrc $(HK)/HomeKit.rdef
	xres -o $@ campiello_homekit.rsrc
	mimeset -f $@
	rm -f campiello_homekit.rsrc

# Build a catalog from its checked-in catkeys. To refresh the catkeys after changing strings, see
# `make catkeys` below.
%.catalog: $(HK)/locales/%.catkeys
	linkcatkeys -l $* -s $(CATALOG_SIG) -o $@ $<

# Regenerate the catkeys template from the source (run after adding/changing B_TRANSLATE strings):
# preprocess in collecting mode (-P drops the line markers that break the key scanner), then collect.
.PHONY: catkeys
catkeys:
	$(CXX) -DB_COLLECTING_CATKEYS -E -P -x c++ -std=c++17 $(HK)/campiello_homekit.cpp > homekit.pre
	$(CXX) -DB_COLLECTING_CATKEYS -E -P -x c++ -std=c++17 $(HK)/HapInfo.cpp >> homekit.pre
	collectcatkeys -s $(CATALOG_SIG) -l it -o $(HK)/locales/it.catkeys homekit.pre
	rm -f homekit.pre
	@echo "Refreshed locales/it.catkeys; merge new keys into the other locales/*.catkeys by hand."

clean:
	rm -rf $(STAGE) campiello_homekit $(CATALOGS) homekit.pre $(PKG)
