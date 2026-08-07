# Build the OPTIONAL Campiello Philips Hue package (.hpkg).
#
# Ships the Hue control panel + its handler manifest. Kept separate from the MIT core because it
# links OpenSSL (Apache-2.0); the core campiello package stays free of it (docs/DEVICE_ADDONS.md).
#
#   make package    # builds campiello_hue-<version>-<arch>.hpkg
#   pkgman install ./campiello_hue-0.1.0-1-x86_64.hpkg
#
# Haiku-only (needs the `package` tool and OpenSSL).

VERSION := 0.1.0-3
ARCH := x86_64
PKG := campiello_hue-$(VERSION)-$(ARCH).hpkg

CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O2

HUE := ../../optional/hue
SRC := $(HUE)/campiello_hue.cpp $(HUE)/HueBridge.cpp
STAGE := stage

# Localization (Haiku Locale Kit). Source strings are Italian and show when no catalog matches the
# user's language; the catalogs here translate them. CATALOG_DIR is the signature with the
# "application/" prefix dropped, which is where BLocaleRoster looks (see the system catalogs).
CATALOG_SIG := application/x-vnd.Campiello-hue
CATALOG_DIR := x-vnd.Campiello-hue
LANGS := en
CATALOGS := $(LANGS:%=%.catalog)

.PHONY: package clean
package: $(PKG)

$(PKG): campiello_hue.PackageInfo campiello_hue $(CATALOGS)
	rm -rf $(STAGE)
	mkdir -p $(STAGE)/apps $(STAGE)/data/campiello/handlers
	mkdir -p $(STAGE)/data/locale/catalogs/$(CATALOG_DIR)
	cp campiello_hue $(STAGE)/apps/campiello_hue
	cp $(HUE)/hue.handler $(STAGE)/data/campiello/handlers/hue.handler
	for l in $(LANGS); do cp $$l.catalog $(STAGE)/data/locale/catalogs/$(CATALOG_DIR)/$$l.catalog; done
	package create -C $(STAGE) -i campiello_hue.PackageInfo $(PKG)
	@echo "Built $(PKG)"

# The control panel, with its app_signature resource so be_roster can launch it by signature;
# liblocalestub so B_TRANSLATE resolves the app's catalog at runtime.
campiello_hue: $(SRC) $(HUE)/HueBridge.h $(HUE)/Hue.rdef
	$(CXX) $(CXXFLAGS) -o $@ $(SRC) -lbe -lssl -lcrypto -lnetwork -llocalestub
	rc -o campiello_hue.rsrc $(HUE)/Hue.rdef
	xres -o $@ campiello_hue.rsrc
	mimeset -f $@
	rm -f campiello_hue.rsrc

# Build a catalog from its checked-in catkeys. To refresh the catkeys after changing strings, see
# `make catkeys` below.
%.catalog: $(HUE)/locales/%.catkeys
	linkcatkeys -l $* -s $(CATALOG_SIG) -o $@ $<

# Regenerate the catkeys template from the source (run after adding/changing B_TRANSLATE strings):
# preprocess in collecting mode (-P drops the line markers that break the key scanner), then collect.
.PHONY: catkeys
catkeys:
	$(CXX) -DB_COLLECTING_CATKEYS -E -P -x c++ -std=c++17 $(HUE)/campiello_hue.cpp > hue.pre
	collectcatkeys -s $(CATALOG_SIG) -l it -o $(HUE)/locales/it.catkeys hue.pre
	rm -f hue.pre
	@echo "Refreshed locales/it.catkeys; merge new keys into the other locales/*.catkeys by hand."

clean:
	rm -rf $(STAGE) campiello_hue $(CATALOGS) hue.pre $(PKG)
