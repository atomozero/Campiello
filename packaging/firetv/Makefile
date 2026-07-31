# Build the OPTIONAL Campiello Fire TV package (.hpkg).
#
# Ships the Fire TV remote app + its handler manifest + its localization catalogs. Kept separate from
# the MIT core because it links OpenSSL (Apache-2.0); the core campiello package stays free of it
# (docs/DEVICE_ADDONS.md).
#
#   make package    # builds campiello_firetv-<version>-<arch>.hpkg
#   pkgman install ./campiello_firetv-0.1.0-6-x86_64.hpkg
#
# Haiku-only (needs the `package` tool, OpenSSL, and the Locale Kit tools).

VERSION := 0.1.0-7
ARCH := x86_64
PKG := campiello_firetv-$(VERSION)-$(ARCH).hpkg

CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O2

FTV := ../../optional/firetv
SRC := $(FTV)/campiello_firetv.cpp $(FTV)/FireTVRemote.cpp
STAGE := stage

# Localization (Haiku Locale Kit). Source strings are Italian and show when no catalog matches the
# user's language; the catalogs here translate them. CATALOG_DIR is the signature with the
# "application/" prefix dropped, which is where BLocaleRoster looks (see the system catalogs).
CATALOG_SIG := application/x-vnd.Campiello-firetv
CATALOG_DIR := x-vnd.Campiello-firetv
LANGS := en
CATALOGS := $(LANGS:%=%.catalog)

.PHONY: package clean
package: $(PKG)

$(PKG): campiello_firetv.PackageInfo campiello_firetv $(CATALOGS)
	rm -rf $(STAGE)
	mkdir -p $(STAGE)/apps $(STAGE)/data/campiello/handlers
	mkdir -p $(STAGE)/data/locale/catalogs/$(CATALOG_DIR)
	cp campiello_firetv $(STAGE)/apps/campiello_firetv
	cp $(FTV)/firetv.handler $(STAGE)/data/campiello/handlers/firetv.handler
	for l in $(LANGS); do cp $$l.catalog $(STAGE)/data/locale/catalogs/$(CATALOG_DIR)/$$l.catalog; done
	package create -C $(STAGE) -i campiello_firetv.PackageInfo $(PKG)
	@echo "Built $(PKG)"

# The remote app: app_signature resource so be_roster can launch it by signature; liblocalestub so
# B_TRANSLATE resolves the app's catalog at runtime.
campiello_firetv: $(SRC) $(FTV)/FireTVRemote.h $(FTV)/FireTVApp.rdef
	$(CXX) $(CXXFLAGS) -o $@ $(SRC) -lbe -lssl -lcrypto -lnetwork -llocalestub
	rc -o campiello_firetv.rsrc $(FTV)/FireTVApp.rdef
	xres -o $@ campiello_firetv.rsrc
	mimeset -f $@
	rm -f campiello_firetv.rsrc

# Build a catalog from its checked-in catkeys. To refresh the catkeys after changing strings, see
# `make catkeys` below.
%.catalog: $(FTV)/locales/%.catkeys
	linkcatkeys -l $* -s $(CATALOG_SIG) -o $@ $<

# Regenerate the catkeys template from the source (run after adding/changing B_TRANSLATE strings):
# preprocess in collecting mode (-P drops the line markers that break the key scanner), then collect.
.PHONY: catkeys
catkeys:
	$(CXX) -DB_COLLECTING_CATKEYS -E -P -x c++ -std=c++17 $(FTV)/campiello_firetv.cpp > firetv.pre
	collectcatkeys -s $(CATALOG_SIG) -l it -o $(FTV)/locales/it.catkeys firetv.pre
	rm -f firetv.pre
	@echo "Refreshed locales/it.catkeys; merge new keys into the other locales/*.catkeys by hand."

clean:
	rm -rf $(STAGE) campiello_firetv $(CATALOGS) firetv.pre $(PKG)
