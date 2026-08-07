# Build the OPTIONAL Campiello NUT (UPS) package (.hpkg).
#
# Ships the UPS monitor + its handler manifest. Pure sockets over the NUT text protocol (no
# third-party dependency), read-only, so this add-on is itself MIT-clean; kept optional only because
# it is device-specific.
#
#   make package    # builds campiello_nut-<version>-<arch>.hpkg
#   pkgman install ./campiello_nut-0.1.0-1-x86_64.hpkg
#
# Haiku-only (needs the `package` tool).

VERSION := 0.1.0-2
ARCH := x86_64
PKG := campiello_nut-$(VERSION)-$(ARCH).hpkg

CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O2

NUT := ../../optional/nut
SRC := $(NUT)/campiello_nut.cpp $(NUT)/NutClient.cpp
STAGE := stage

# Localization (Haiku Locale Kit). Source strings are Italian and show when no catalog matches the
# user's language; the catalogs here translate them. CATALOG_DIR is the signature with the
# "application/" prefix dropped, which is where BLocaleRoster looks (see the system catalogs).
CATALOG_SIG := application/x-vnd.Campiello-nut
CATALOG_DIR := x-vnd.Campiello-nut
LANGS := en
CATALOGS := $(LANGS:%=%.catalog)

.PHONY: package clean test
package: $(PKG)

$(PKG): campiello_nut.PackageInfo campiello_nut $(CATALOGS)
	rm -rf $(STAGE)
	mkdir -p $(STAGE)/apps $(STAGE)/data/campiello/handlers
	mkdir -p $(STAGE)/data/locale/catalogs/$(CATALOG_DIR)
	cp campiello_nut $(STAGE)/apps/campiello_nut
	cp $(NUT)/nut.handler $(STAGE)/data/campiello/handlers/nut.handler
	for l in $(LANGS); do cp $$l.catalog $(STAGE)/data/locale/catalogs/$(CATALOG_DIR)/$$l.catalog; done
	package create -C $(STAGE) -i campiello_nut.PackageInfo $(PKG)
	@echo "Built $(PKG)"

# The UPS monitor, with its app_signature resource so be_roster can launch it by signature;
# liblocalestub so B_TRANSLATE resolves the app's catalog at runtime.
campiello_nut: $(SRC) $(NUT)/NutClient.h $(NUT)/Nut.rdef
	$(CXX) $(CXXFLAGS) -o $@ $(SRC) -lbe -lnetwork -llocalestub
	rc -o campiello_nut.rsrc $(NUT)/Nut.rdef
	xres -o $@ campiello_nut.rsrc
	mimeset -f $@
	rm -f campiello_nut.rsrc

# Build a catalog from its checked-in catkeys. To refresh the catkeys after changing strings, see
# `make catkeys` below.
%.catalog: $(NUT)/locales/%.catkeys
	linkcatkeys -l $* -s $(CATALOG_SIG) -o $@ $<

# Regenerate the catkeys template from the source (run after adding/changing B_TRANSLATE strings):
# preprocess in collecting mode (-P drops the line markers that break the key scanner), then collect.
.PHONY: catkeys
catkeys:
	$(CXX) -DB_COLLECTING_CATKEYS -E -P -x c++ -std=c++17 $(NUT)/campiello_nut.cpp > nut.pre
	collectcatkeys -s $(CATALOG_SIG) -l it -o $(NUT)/locales/it.catkeys nut.pre
	rm -f nut.pre

# Protocol parser unit test (no network).
test:
	$(CXX) $(CXXFLAGS) -o test_nut $(NUT)/test_nut.cpp $(NUT)/NutClient.cpp -lnetwork
	./test_nut

clean:
	rm -rf $(STAGE) campiello_nut test_nut $(PKG) $(CATALOGS) nut.pre
