# Build the OPTIONAL Campiello Shelly package (.hpkg).
#
# Ships the Shelly control panel + its handler manifest. It talks to a Shelly relay/plug over its
# LOCAL HTTP API (gen1 REST and gen2 RPC, auto-detected): lists the channels with an on/off button
# and a live power readout, and opens the device web UI. No third-party dependency (plain sockets +
# a tiny JSON scanner), so the add-on stays MIT-clean; kept optional only because it is
# device-specific.
#
#   make package    # builds campiello_shelly-<version>-<arch>.hpkg
#   pkgman install ./campiello_shelly-0.1.0-4-x86_64.hpkg
#
# Haiku-only (needs the `package` tool).

VERSION := 0.1.0-8
ARCH := x86_64
PKG := campiello_shelly-$(VERSION)-$(ARCH).hpkg

CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O2

SH := ../../optional/shelly
SRC := $(SH)/campiello_shelly.cpp $(SH)/ShellyClient.cpp $(SH)/ShellyPowerLog.cpp
# The background power logger: reads the watch list, polls each Shelly once a minute and appends to
# its 48h power log. No libbe (headless), only the socket client and the plain-text log helper.
LOGD_SRC := $(SH)/shelly_logd.cpp $(SH)/ShellyClient.cpp $(SH)/ShellyPowerLog.cpp
STAGE := stage

# Localization (Haiku Locale Kit). Source strings are Italian and show when no catalog matches the
# user's language; the catalogs here translate them. CATALOG_DIR is the signature with the
# "application/" prefix dropped, which is where BLocaleRoster looks (see the system catalogs).
CATALOG_SIG := application/x-vnd.Campiello-shelly
CATALOG_DIR := x-vnd.Campiello-shelly
LANGS := en
CATALOGS := $(LANGS:%=%.catalog)

.PHONY: package clean
package: $(PKG)

$(PKG): campiello_shelly.PackageInfo campiello_shelly campiello_shelly_logd $(CATALOGS)
	rm -rf $(STAGE)
	mkdir -p $(STAGE)/apps $(STAGE)/bin $(STAGE)/data/campiello/handlers
	mkdir -p $(STAGE)/data/locale/catalogs/$(CATALOG_DIR) $(STAGE)/data/user_launch
	cp campiello_shelly $(STAGE)/apps/campiello_shelly
	cp campiello_shelly_logd $(STAGE)/bin/campiello_shelly_logd
	cp $(SH)/shelly.handler $(STAGE)/data/campiello/handlers/shelly.handler
	cp launch/campiello_shelly $(STAGE)/data/user_launch/campiello_shelly
	for l in $(LANGS); do cp $$l.catalog $(STAGE)/data/locale/catalogs/$(CATALOG_DIR)/$$l.catalog; done
	package create -C $(STAGE) -i campiello_shelly.PackageInfo $(PKG)
	@echo "Built $(PKG)"

# The control panel, with its app_signature resource so be_roster can launch it by signature;
# liblocalestub so B_TRANSLATE resolves the app's catalog at runtime.
campiello_shelly: $(SRC) $(SH)/ShellyClient.h $(SH)/ShellyPowerLog.h $(SH)/Shelly.rdef
	$(CXX) $(CXXFLAGS) -o $@ $(SRC) -lbe -lnetwork -llocalestub
	rc -o campiello_shelly.rsrc $(SH)/Shelly.rdef
	xres -o $@ campiello_shelly.rsrc
	mimeset -f $@
	rm -f campiello_shelly.rsrc

# The headless background logger. No libbe, only the network kit (sockets).
campiello_shelly_logd: $(LOGD_SRC) $(SH)/ShellyClient.h $(SH)/ShellyPowerLog.h
	$(CXX) $(CXXFLAGS) -o $@ $(LOGD_SRC) -lnetwork

# Build a catalog from its checked-in catkeys. To refresh the catkeys after changing strings, see
# `make catkeys` below.
%.catalog: $(SH)/locales/%.catkeys
	linkcatkeys -l $* -s $(CATALOG_SIG) -o $@ $<

# Regenerate the catkeys template from the source (run after adding/changing B_TRANSLATE strings):
# preprocess in collecting mode (-P drops the line markers that break the key scanner), then collect.
.PHONY: catkeys
catkeys:
	$(CXX) -DB_COLLECTING_CATKEYS -E -P -x c++ -std=c++17 $(SH)/campiello_shelly.cpp > shelly.pre
	collectcatkeys -s $(CATALOG_SIG) -l it -o $(SH)/locales/it.catkeys shelly.pre
	rm -f shelly.pre
	@echo "Refreshed locales/it.catkeys; merge new keys into the other locales/*.catkeys by hand."

clean:
	rm -rf $(STAGE) campiello_shelly campiello_shelly_logd $(CATALOGS) shelly.pre $(PKG)
