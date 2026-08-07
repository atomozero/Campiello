# Build the OPTIONAL Campiello NFS package (.hpkg).
#
# Ships the NFS export lister + its handler manifest. Pure RPC over UDP (no third-party dependency),
# so this add-on is itself MIT-clean; kept optional only because it is device-specific.
#
#   make package    # builds campiello_nfs-<version>-<arch>.hpkg
#   pkgman install ./campiello_nfs-0.1.0-1-x86_64.hpkg
#
# Haiku-only (needs the `package` tool).

VERSION := 0.1.0-3
ARCH := x86_64
PKG := campiello_nfs-$(VERSION)-$(ARCH).hpkg

CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O2

NFS := ../../optional/nfs
SRC := $(NFS)/campiello_nfs.cpp $(NFS)/NfsProbe.cpp
STAGE := stage

# Localization (Haiku Locale Kit). Source strings are Italian and show when no catalog matches the
# user's language; the catalogs here translate them. CATALOG_DIR is the signature with the
# "application/" prefix dropped, which is where BLocaleRoster looks (see the system catalogs).
CATALOG_SIG := application/x-vnd.Campiello-nfs
CATALOG_DIR := x-vnd.Campiello-nfs
LANGS := en
CATALOGS := $(LANGS:%=%.catalog)

.PHONY: package clean
package: $(PKG)

$(PKG): campiello_nfs.PackageInfo campiello_nfs $(CATALOGS)
	rm -rf $(STAGE)
	mkdir -p $(STAGE)/apps $(STAGE)/data/campiello/handlers
	mkdir -p $(STAGE)/data/locale/catalogs/$(CATALOG_DIR)
	cp campiello_nfs $(STAGE)/apps/campiello_nfs
	cp $(NFS)/nfs.handler $(STAGE)/data/campiello/handlers/nfs.handler
	for l in $(LANGS); do cp $$l.catalog $(STAGE)/data/locale/catalogs/$(CATALOG_DIR)/$$l.catalog; done
	package create -C $(STAGE) -i campiello_nfs.PackageInfo $(PKG)
	@echo "Built $(PKG)"

# The export lister, with its app_signature resource so be_roster can launch it by signature;
# liblocalestub so B_TRANSLATE resolves the app's catalog at runtime.
campiello_nfs: $(SRC) $(NFS)/NfsProbe.h $(NFS)/Nfs.rdef
	$(CXX) $(CXXFLAGS) -o $@ $(SRC) -lbe -lnetwork -llocalestub
	rc -o campiello_nfs.rsrc $(NFS)/Nfs.rdef
	xres -o $@ campiello_nfs.rsrc
	mimeset -f $@
	rm -f campiello_nfs.rsrc

# Build a catalog from its checked-in catkeys. To refresh the catkeys after changing strings, see
# `make catkeys` below.
%.catalog: $(NFS)/locales/%.catkeys
	linkcatkeys -l $* -s $(CATALOG_SIG) -o $@ $<

# Regenerate the catkeys template from the source (run after adding/changing B_TRANSLATE strings):
# preprocess in collecting mode (-P drops the line markers that break the key scanner), then collect.
.PHONY: catkeys
catkeys:
	$(CXX) -DB_COLLECTING_CATKEYS -E -P -x c++ -std=c++17 $(NFS)/campiello_nfs.cpp > nfs.pre
	collectcatkeys -s $(CATALOG_SIG) -l it -o $(NFS)/locales/it.catkeys nfs.pre
	rm -f nfs.pre
	@echo "Refreshed locales/it.catkeys; merge new keys into the other locales/*.catkeys by hand."

clean:
	rm -rf $(STAGE) campiello_nfs $(CATALOGS) nfs.pre $(PKG)
