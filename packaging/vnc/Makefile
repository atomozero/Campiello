# Build the OPTIONAL Campiello VNC remote-desktop package (.hpkg).
#
# Ships the VNC launcher + its handler manifest. No third-party dependency (it hands off to an
# installed viewer), so this add-on is itself MIT-clean; kept optional only because it is
# device-specific.
#
#   make package    # builds campiello_vnc-<version>-<arch>.hpkg
#   pkgman install ./campiello_vnc-0.1.0-1-x86_64.hpkg
#
# Haiku-only (needs the `package` tool).

VERSION := 0.1.0-3
ARCH := x86_64
PKG := campiello_vnc-$(VERSION)-$(ARCH).hpkg

CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O2

VNC := ../../optional/vnc
SRC := $(VNC)/campiello_vnc.cpp
STAGE := stage

# Localization (Haiku Locale Kit). Source strings are Italian and show when no catalog matches the
# user's language; the catalogs here translate them. CATALOG_DIR is the signature with the
# "application/" prefix dropped, which is where BLocaleRoster looks (see the system catalogs).
CATALOG_SIG := application/x-vnd.Campiello-vnc
CATALOG_DIR := x-vnd.Campiello-vnc
LANGS := en
CATALOGS := $(LANGS:%=%.catalog)

.PHONY: package clean
package: $(PKG)

$(PKG): campiello_vnc.PackageInfo campiello_vnc $(CATALOGS)
	rm -rf $(STAGE)
	mkdir -p $(STAGE)/apps $(STAGE)/data/campiello/handlers
	mkdir -p $(STAGE)/data/locale/catalogs/$(CATALOG_DIR)
	cp campiello_vnc $(STAGE)/apps/campiello_vnc
	cp $(VNC)/vnc.handler $(STAGE)/data/campiello/handlers/vnc.handler
	for l in $(LANGS); do cp $$l.catalog $(STAGE)/data/locale/catalogs/$(CATALOG_DIR)/$$l.catalog; done
	package create -C $(STAGE) -i campiello_vnc.PackageInfo $(PKG)
	@echo "Built $(PKG)"

# The launcher, with its app_signature resource so be_roster can launch it by signature; liblocalestub
# so B_TRANSLATE resolves the app's catalog at runtime.
campiello_vnc: $(SRC) $(VNC)/Vnc.rdef
	$(CXX) $(CXXFLAGS) -o $@ $(SRC) -lbe -llocalestub
	rc -o campiello_vnc.rsrc $(VNC)/Vnc.rdef
	xres -o $@ campiello_vnc.rsrc
	mimeset -f $@
	rm -f campiello_vnc.rsrc

# Build a catalog from its checked-in catkeys. To refresh the catkeys after changing strings, see
# `make catkeys` below.
%.catalog: $(VNC)/locales/%.catkeys
	linkcatkeys -l $* -s $(CATALOG_SIG) -o $@ $<

# Regenerate the catkeys template from the source (run after adding/changing B_TRANSLATE strings):
# preprocess in collecting mode (-P drops the line markers that break the key scanner), then collect.
.PHONY: catkeys
catkeys:
	$(CXX) -DB_COLLECTING_CATKEYS -E -P -x c++ -std=c++17 $(VNC)/campiello_vnc.cpp > vnc.pre
	collectcatkeys -s $(CATALOG_SIG) -l it -o $(VNC)/locales/it.catkeys vnc.pre
	rm -f vnc.pre
	@echo "Refreshed locales/it.catkeys; merge new keys into the other locales/*.catkeys by hand."

clean:
	rm -rf $(STAGE) campiello_vnc $(CATALOGS) vnc.pre $(PKG)
