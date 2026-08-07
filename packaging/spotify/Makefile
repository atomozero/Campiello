# Build the OPTIONAL Campiello Spotify Connect package (.hpkg).
#
# Ships the Spotify Connect info add-on + its handler manifest. Pure HTTP (no third-party dependency),
# so this add-on is itself MIT-clean; kept optional only because it is device-specific.
#
#   make package    # builds campiello_spotify-<version>-<arch>.hpkg
#   pkgman install ./campiello_spotify-0.1.0-1-x86_64.hpkg
#
# Haiku-only (needs the `package` tool).

VERSION := 0.1.0-4
ARCH := x86_64
PKG := campiello_spotify-$(VERSION)-$(ARCH).hpkg

CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O2

SPT := ../../optional/spotify
SRC := $(SPT)/campiello_spotify.cpp $(SPT)/SpotifyProbe.cpp
STAGE := stage

# Localization (Haiku Locale Kit). Source strings are Italian and show when no catalog matches the
# user's language; the catalogs here translate them. CATALOG_DIR is the signature with the
# "application/" prefix dropped, which is where BLocaleRoster looks (see the system catalogs).
CATALOG_SIG := application/x-vnd.Campiello-spotify
CATALOG_DIR := x-vnd.Campiello-spotify
LANGS := en
CATALOGS := $(LANGS:%=%.catalog)

.PHONY: package clean
package: $(PKG)

$(PKG): campiello_spotify.PackageInfo campiello_spotify $(CATALOGS)
	rm -rf $(STAGE)
	mkdir -p $(STAGE)/apps $(STAGE)/data/campiello/handlers
	mkdir -p $(STAGE)/data/locale/catalogs/$(CATALOG_DIR)
	cp campiello_spotify $(STAGE)/apps/campiello_spotify
	cp $(SPT)/spotify.handler $(STAGE)/data/campiello/handlers/spotify.handler
	for l in $(LANGS); do cp $$l.catalog $(STAGE)/data/locale/catalogs/$(CATALOG_DIR)/$$l.catalog; done
	package create -C $(STAGE) -i campiello_spotify.PackageInfo $(PKG)
	@echo "Built $(PKG)"

# The info app, with its app_signature resource so be_roster can launch it by signature; liblocalestub
# so B_TRANSLATE resolves the app's catalog at runtime.
campiello_spotify: $(SRC) $(SPT)/SpotifyProbe.h $(SPT)/Spotify.rdef
	$(CXX) $(CXXFLAGS) -o $@ $(SRC) -lbe -lnetwork -llocalestub
	rc -o campiello_spotify.rsrc $(SPT)/Spotify.rdef
	xres -o $@ campiello_spotify.rsrc
	mimeset -f $@
	rm -f campiello_spotify.rsrc

# Build a catalog from its checked-in catkeys. To refresh the catkeys after changing strings, see
# `make catkeys` below.
%.catalog: $(SPT)/locales/%.catkeys
	linkcatkeys -l $* -s $(CATALOG_SIG) -o $@ $<

# Regenerate the catkeys template from the source (run after adding/changing B_TRANSLATE strings):
# preprocess in collecting mode (-P drops the line markers that break the key scanner), then collect.
.PHONY: catkeys
catkeys:
	$(CXX) -DB_COLLECTING_CATKEYS -E -P -x c++ -std=c++17 $(SPT)/campiello_spotify.cpp > spotify.pre
	collectcatkeys -s $(CATALOG_SIG) -l it -o $(SPT)/locales/it.catkeys spotify.pre
	rm -f spotify.pre
	@echo "Refreshed locales/it.catkeys; merge new keys into the other locales/*.catkeys by hand."

clean:
	rm -rf $(STAGE) campiello_spotify $(CATALOGS) spotify.pre $(PKG)
