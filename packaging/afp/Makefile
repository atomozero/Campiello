# Build the OPTIONAL Campiello AFP package (.hpkg).
#
# Ships the AFP info probe + its handler manifest + its localization catalogs. Pure DSI over TCP (no
# third-party dependency), so this add-on is itself MIT-clean; kept optional only because it is
# device-specific.
#
#   make package    # builds campiello_afp-<version>-<arch>.hpkg
#   pkgman install ./campiello_afp-0.1.0-1-x86_64.hpkg
#
# Haiku-only (needs the `package` tool and the Locale Kit tools).

VERSION := 0.1.0-3
ARCH := x86_64
PKG := campiello_afp-$(VERSION)-$(ARCH).hpkg

CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O2

AFP := ../../optional/afp
SRC := $(AFP)/campiello_afp.cpp $(AFP)/AfpProbe.cpp
STAGE := stage

# Localization (Haiku Locale Kit). Source strings are Italian and show when no catalog matches the
# user's language; the catalogs here translate them. CATALOG_DIR is the signature with the
# "application/" prefix dropped, which is where BLocaleRoster looks (see the system catalogs).
CATALOG_SIG := application/x-vnd.Campiello-afp
CATALOG_DIR := x-vnd.Campiello-afp
LANGS := en
CATALOGS := $(LANGS:%=%.catalog)

.PHONY: package clean
package: $(PKG)

$(PKG): campiello_afp.PackageInfo campiello_afp $(CATALOGS)
	rm -rf $(STAGE)
	mkdir -p $(STAGE)/apps $(STAGE)/data/campiello/handlers
	mkdir -p $(STAGE)/data/locale/catalogs/$(CATALOG_DIR)
	cp campiello_afp $(STAGE)/apps/campiello_afp
	cp $(AFP)/afp.handler $(STAGE)/data/campiello/handlers/afp.handler
	for l in $(LANGS); do cp $$l.catalog $(STAGE)/data/locale/catalogs/$(CATALOG_DIR)/$$l.catalog; done
	package create -C $(STAGE) -i campiello_afp.PackageInfo $(PKG)
	@echo "Built $(PKG)"

# The info probe, with its app_signature resource so be_roster can launch it by signature; liblocalestub
# so B_TRANSLATE resolves the app's catalog at runtime.
campiello_afp: $(SRC) $(AFP)/AfpProbe.h $(AFP)/Afp.rdef
	$(CXX) $(CXXFLAGS) -o $@ $(SRC) -lbe -lnetwork -llocalestub
	rc -o campiello_afp.rsrc $(AFP)/Afp.rdef
	xres -o $@ campiello_afp.rsrc
	mimeset -f $@
	rm -f campiello_afp.rsrc

# Build a catalog from its checked-in catkeys. To refresh the catkeys after changing strings, see
# `make catkeys` below.
%.catalog: $(AFP)/locales/%.catkeys
	linkcatkeys -l $* -s $(CATALOG_SIG) -o $@ $<

# Regenerate the catkeys template from the source (run after adding/changing B_TRANSLATE strings):
# preprocess in collecting mode (-P drops the line markers that break the key scanner), then collect.
.PHONY: catkeys
catkeys:
	$(CXX) -DB_COLLECTING_CATKEYS -E -P -x c++ -std=c++17 $(AFP)/campiello_afp.cpp > afp.pre
	collectcatkeys -s $(CATALOG_SIG) -l it -o $(AFP)/locales/it.catkeys afp.pre
	rm -f afp.pre
	@echo "Refreshed locales/it.catkeys; merge new keys into the other locales/*.catkeys by hand."

clean:
	rm -rf $(STAGE) campiello_afp $(CATALOGS) afp.pre $(PKG)
