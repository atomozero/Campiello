# Build the OPTIONAL Campiello IPP printing package (.hpkg).
#
# Ships the IPP printer panel + its handler manifest. Pure IPP (no third-party dependency), so this
# add-on is itself MIT-clean; it is kept optional only because it is device-specific.
#
#   make package    # builds campiello_ipp-<version>-<arch>.hpkg
#   pkgman install ./campiello_ipp-0.1.0-1-x86_64.hpkg
#
# Haiku-only (needs the `package` tool).

VERSION := 0.1.0-3
ARCH := x86_64
PKG := campiello_ipp-$(VERSION)-$(ARCH).hpkg

CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O2

IPP := ../../optional/ipp
SRC := $(IPP)/campiello_ipp.cpp $(IPP)/IppClient.cpp
STAGE := stage

# Localization (Haiku Locale Kit). Source strings are Italian and show when no catalog matches the
# user's language; the catalogs here translate them. CATALOG_DIR is the signature with the
# "application/" prefix dropped, which is where BLocaleRoster looks (see the system catalogs).
CATALOG_SIG := application/x-vnd.Campiello-ipp
CATALOG_DIR := x-vnd.Campiello-ipp
LANGS := en
CATALOGS := $(LANGS:%=%.catalog)

.PHONY: package clean
package: $(PKG)

$(PKG): campiello_ipp.PackageInfo campiello_ipp $(CATALOGS)
	rm -rf $(STAGE)
	mkdir -p $(STAGE)/apps $(STAGE)/data/campiello/handlers
	mkdir -p $(STAGE)/data/locale/catalogs/$(CATALOG_DIR)
	cp campiello_ipp $(STAGE)/apps/campiello_ipp
	cp $(IPP)/ipp.handler $(STAGE)/data/campiello/handlers/ipp.handler
	for l in $(LANGS); do cp $$l.catalog $(STAGE)/data/locale/catalogs/$(CATALOG_DIR)/$$l.catalog; done
	package create -C $(STAGE) -i campiello_ipp.PackageInfo $(PKG)
	@echo "Built $(PKG)"

# The printer panel, with its app_signature resource so be_roster can launch it by signature;
# liblocalestub so B_TRANSLATE resolves the app's catalog at runtime.
# BFilePanel (the "Stampa un file..." picker) lives in libtracker.
campiello_ipp: $(SRC) $(IPP)/IppClient.h $(IPP)/Ipp.rdef
	$(CXX) $(CXXFLAGS) -o $@ $(SRC) -lbe -lnetwork -ltracker -llocalestub
	rc -o campiello_ipp.rsrc $(IPP)/Ipp.rdef
	xres -o $@ campiello_ipp.rsrc
	mimeset -f $@
	rm -f campiello_ipp.rsrc

# Build a catalog from its checked-in catkeys. To refresh the catkeys after changing strings, see
# `make catkeys` below.
%.catalog: $(IPP)/locales/%.catkeys
	linkcatkeys -l $* -s $(CATALOG_SIG) -o $@ $<

# Regenerate the catkeys template from the source (run after adding/changing B_TRANSLATE strings):
# preprocess in collecting mode (-P drops the line markers that break the key scanner), then collect.
.PHONY: catkeys
catkeys:
	$(CXX) -DB_COLLECTING_CATKEYS -E -P -x c++ -std=c++17 $(IPP)/campiello_ipp.cpp > ipp.pre
	collectcatkeys -s $(CATALOG_SIG) -l it -o $(IPP)/locales/it.catkeys ipp.pre
	rm -f ipp.pre
	@echo "Refreshed locales/it.catkeys; merge new keys into the other locales/*.catkeys by hand."

clean:
	rm -rf $(STAGE) campiello_ipp $(CATALOGS) ipp.pre $(PKG)
