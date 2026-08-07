# Build the OPTIONAL Campiello eSCL scanning package (.hpkg).
#
# Ships the eSCL scanner panel + its handler manifest. Pure HTTP/XML (no third-party dependency), so
# this add-on is itself MIT-clean; kept optional only because it is device-specific.
#
#   make package    # builds campiello_escl-<version>-<arch>.hpkg
#   pkgman install ./campiello_escl-0.1.0-1-x86_64.hpkg
#
# Haiku-only (needs the `package` tool).

VERSION := 0.1.0-3
ARCH := x86_64
PKG := campiello_escl-$(VERSION)-$(ARCH).hpkg

CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O2

ESCL := ../../optional/escl
SRC := $(ESCL)/campiello_escl.cpp $(ESCL)/EsclClient.cpp
STAGE := stage

# Localization (Haiku Locale Kit). Source strings are Italian and show when no catalog matches the
# user's language; the catalogs here translate them. CATALOG_DIR is the signature with the
# "application/" prefix dropped, which is where BLocaleRoster looks (see the system catalogs).
CATALOG_SIG := application/x-vnd.Campiello-escl
CATALOG_DIR := x-vnd.Campiello-escl
LANGS := en
CATALOGS := $(LANGS:%=%.catalog)

.PHONY: package clean
package: $(PKG)

$(PKG): campiello_escl.PackageInfo campiello_escl $(CATALOGS)
	rm -rf $(STAGE)
	mkdir -p $(STAGE)/apps $(STAGE)/data/campiello/handlers
	mkdir -p $(STAGE)/data/locale/catalogs/$(CATALOG_DIR)
	cp campiello_escl $(STAGE)/apps/campiello_escl
	cp $(ESCL)/escl.handler $(STAGE)/data/campiello/handlers/escl.handler
	for l in $(LANGS); do cp $$l.catalog $(STAGE)/data/locale/catalogs/$(CATALOG_DIR)/$$l.catalog; done
	package create -C $(STAGE) -i campiello_escl.PackageInfo $(PKG)
	@echo "Built $(PKG)"

# The scanner panel, with its app_signature resource so be_roster can launch it by signature;
# liblocalestub so B_TRANSLATE resolves the app's catalog at runtime.
# BFilePanel (the save picker) lives in libtracker.
campiello_escl: $(SRC) $(ESCL)/EsclClient.h $(ESCL)/Escl.rdef
	$(CXX) $(CXXFLAGS) -o $@ $(SRC) -lbe -lnetwork -ltracker -llocalestub
	rc -o campiello_escl.rsrc $(ESCL)/Escl.rdef
	xres -o $@ campiello_escl.rsrc
	mimeset -f $@
	rm -f campiello_escl.rsrc

# Build a catalog from its checked-in catkeys. To refresh the catkeys after changing strings, see
# `make catkeys` below.
%.catalog: $(ESCL)/locales/%.catkeys
	linkcatkeys -l $* -s $(CATALOG_SIG) -o $@ $<

# Regenerate the catkeys template from the source (run after adding/changing B_TRANSLATE strings):
# preprocess in collecting mode (-P drops the line markers that break the key scanner), then collect.
.PHONY: catkeys
catkeys:
	$(CXX) -DB_COLLECTING_CATKEYS -E -P -x c++ -std=c++17 $(ESCL)/campiello_escl.cpp > escl.pre
	collectcatkeys -s $(CATALOG_SIG) -l it -o $(ESCL)/locales/it.catkeys escl.pre
	rm -f escl.pre
	@echo "Refreshed locales/it.catkeys; merge new keys into the other locales/*.catkeys by hand."

clean:
	rm -rf $(STAGE) campiello_escl $(CATALOGS) escl.pre $(PKG)
