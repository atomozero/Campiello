# Build the Campiello Haiku package (.hpkg).
#
# Ships both surfaces now:
#   - interop SFTP: the campiello_sftp userlandfs add-on + the campiello_mount connect helper.
#   - native Campiello: the resident campiello_daemon (advertises _campiello._tcp and serves the
#     shared folder) + the campiello_net discovery add-on (the /Campiello network-neighborhood
#     volume). The daemon auto-starts at login via a user launch job
#     (data/user_launch/campiello).
#
#   make package    # builds campiello-<version>-<arch>.hpkg
#   pkgman install ./campiello-0.2.2-1-x86_64.hpkg
#
# Haiku-only (needs the `package` tool, the userlandfs FUSE headers, libssh2, and OpenSSL).

VERSION := 0.3.33-1
ARCH := x86_64
PKG := campiello-$(VERSION)-$(ARCH).hpkg

# Localization (Haiku Locale Kit) for the WON app. Source strings are Italian and show when no
# catalog matches the user's language; the catalogs here translate them. CATALOG_DIR is the WON
# signature with the "application/" prefix dropped, where BLocaleRoster looks for catalogs.
CATALOG_SIG := application/x-vnd.Campiello-won
CATALOG_DIR := x-vnd.Campiello-won
LANGS := en
CATALOGS := $(LANGS:%=%.catalog)

CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -Werror -O2 -pthread
FUSE_HEADERS := /boot/system/develop/headers/private/userlandfs/fuse

SRC := ../src
FUSE_DIR := $(SRC)/fondamenta/fuse
BE_DIR := $(SRC)/fondamenta/backend
MOUNT_DIR := $(SRC)/fondamenta/mount
DISC_DIR := $(SRC)/fondamenta/discovery
REPL_DIR := $(SRC)/bricola/replicant
SRV_DIR := $(SRC)/traghetto/server
WIRE_DIR := $(SRC)/traghetto/wire
NET_DIR := $(SRC)/traghetto/transport
TLS_DIR := $(SRC)/traghetto/tls
DISP_DIR := $(SRC)/traghetto/dispatch
TRUST_DIR := $(SRC)/traghetto/trust
MDNS_DIR := $(SRC)/bricola/mdns
VIC_DIR := $(SRC)/vicinato

# Shared Campiello application icon (a world/globe, BEOS:ICON), compiled alongside each app rdef.
ICON_RDEF := ../art/world_icon.rdef

# Shared source groups.
WIRE := $(WIRE_DIR)/FrameCodec.cpp $(WIRE_DIR)/Cbor.cpp $(WIRE_DIR)/Handshake.cpp \
        $(WIRE_DIR)/Error.cpp $(WIRE_DIR)/Listing.cpp $(WIRE_DIR)/Attributes.cpp \
        $(WIRE_DIR)/FileOps.cpp $(WIRE_DIR)/Namespace.cpp $(WIRE_DIR)/AttrOps.cpp \
        $(WIRE_DIR)/Query.cpp
MDNS := $(MDNS_DIR)/Bricola.cpp $(MDNS_DIR)/Responder.cpp $(MDNS_DIR)/Browser.cpp \
        $(MDNS_DIR)/PeerTable.cpp $(MDNS_DIR)/MdnsSocket.cpp $(MDNS_DIR)/MdnsWire.cpp
TLS := $(TLS_DIR)/TlsConnection.cpp $(TLS_DIR)/Identity.cpp $(TLS_DIR)/Fingerprint.cpp

# Interop SFTP.
SFTP_ADDON := $(FUSE_DIR)/campiello_sftp_main.cpp $(FUSE_DIR)/CampielloFuse.cpp \
              $(FUSE_DIR)/FuseTranslate.cpp $(FUSE_DIR)/SftpMount.cpp \
              $(BE_DIR)/SftpBackend.cpp $(BE_DIR)/SftpKnownHosts.cpp
HELPER := $(MOUNT_DIR)/campiello_mount.cpp $(FUSE_DIR)/SftpMount.cpp \
          $(BE_DIR)/SftpBackend.cpp $(BE_DIR)/SftpKnownHosts.cpp

# Native daemon (resident node: TLS server + pairing gate + FileServer + Bricola advertise).
DAEMON := $(SRV_DIR)/daemon_main.cpp $(SRV_DIR)/DaemonApp.cpp $(SRV_DIR)/HaikuPairingPrompt.cpp \
          $(SRV_DIR)/ServerNode.cpp $(SRV_DIR)/GatedHandler.cpp $(SRV_DIR)/FileServer.cpp \
          $(SRV_DIR)/Daemon.cpp $(DISP_DIR)/Dispatch.cpp $(DISP_DIR)/FrameWriter.cpp $(NET_DIR)/Connection.cpp \
          $(TLS) $(TRUST_DIR)/Pairing.cpp $(TRUST_DIR)/TokenBucket.cpp \
          $(TRUST_DIR)/TrustStore.cpp $(TRUST_DIR)/Paths.cpp $(WIRE) $(MDNS)

# Native discovery add-on (client-only: /Campiello network neighborhood).
NET_ADDON := $(DISC_DIR)/campiello_net_main.cpp $(DISC_DIR)/BricolaDirectory.cpp \
             $(DISC_DIR)/ConnectionManager.cpp $(DISC_DIR)/PathRouter.cpp \
             $(DISC_DIR)/QueryAggregator.cpp \
             $(FUSE_DIR)/CampielloFuse.cpp $(FUSE_DIR)/FuseTranslate.cpp $(BE_DIR)/CnpBackend.cpp \
             $(TLS) $(TRUST_DIR)/TrustStore.cpp $(NET_DIR)/Connection.cpp $(DISP_DIR)/Dispatch.cpp $(DISP_DIR)/FrameWriter.cpp \
             $(WIRE) $(MDNS)

# Deskbar replicant (peer presence): a shelf add-on browsing peers with Bricola.
REPLICANT := $(REPL_DIR)/PeerReplicant.cpp $(MDNS)

# Vicinato network-neighborhood app (MIT, no libsmb2): discovery + service list; launches the
# SMB/SFTP helpers by signature. Links only the mDNS socket/wire (not the whole Bricola stack).
VICINATO := $(VIC_DIR)/campiello_vicinato.cpp $(VIC_DIR)/NetworkDirectory.cpp \
            $(VIC_DIR)/SmbHostFinder.cpp $(VIC_DIR)/ShareFolder.cpp \
            $(VIC_DIR)/HandlerRegistry.cpp $(VIC_DIR)/NetIntel.cpp $(VIC_DIR)/OldSchoolDemo.cpp \
            $(MDNS_DIR)/MdnsRadar.cpp \
            $(MDNS_DIR)/RadarLabels.cpp $(MDNS_DIR)/MdnsSocket.cpp $(MDNS_DIR)/MdnsWire.cpp

STAGE := stage

.PHONY: package clean
package: $(PKG)

$(PKG): campiello.PackageInfo launch/campiello \
        campiello_sftp campiello_mount campiello_daemon campiello_net campiello_replicant \
        campiello_won $(CATALOGS)
	rm -rf $(STAGE)
	mkdir -p $(STAGE)/add-ons/userlandfs $(STAGE)/apps $(STAGE)/bin $(STAGE)/data/user_launch \
	    $(STAGE)/data/campiello/icons $(STAGE)/data/locale/catalogs/$(CATALOG_DIR)
	cp campiello_sftp $(STAGE)/add-ons/userlandfs/campiello_sftp
	cp campiello_net  $(STAGE)/add-ons/userlandfs/campiello_net
	cp campiello_mount $(STAGE)/apps/campiello_mount
	cp campiello_replicant $(STAGE)/apps/campiello_replicant
	cp campiello_won $(STAGE)/apps/campiello_won
	cp campiello_daemon $(STAGE)/bin/campiello_daemon
	cp launch/campiello $(STAGE)/data/user_launch/campiello
	# The per-service device icons (MIT, from threedeyes' HVIF-Collection; see optional/icons/
	# ATTRIBUTION). The WON app renders <name>.hvif per discovered service.
	cp ../optional/icons/*.hvif $(STAGE)/data/campiello/icons/
	# The WON app's localization catalogs (Haiku Locale Kit).
	for l in $(LANGS); do cp $$l.catalog $(STAGE)/data/locale/catalogs/$(CATALOG_DIR)/$$l.catalog; done
	package create -C $(STAGE) -i campiello.PackageInfo $(PKG)
	@echo "Built $(PKG)"

campiello_sftp: $(SFTP_ADDON)
	$(CXX) $(CXXFLAGS) -shared -fPIC -D_FILE_OFFSET_BITS=64 -I$(FUSE_HEADERS) \
	    -o $@ $(SFTP_ADDON) -luserlandfs_fuse -lssh2

# The SFTP connect helper. Its app_signature resource (mimeset-registered) lets be_roster find it
# by signature so the Vicinato can open it on a double-click.
campiello_mount: $(HELPER) $(MOUNT_DIR)/Mount.rdef
	$(CXX) $(CXXFLAGS) -o $@ $(HELPER) -lbe -lssh2 -lnetwork
	rc -o campiello_mount.rsrc $(MOUNT_DIR)/Mount.rdef
	xres -o $@ campiello_mount.rsrc
	mimeset -f $@
	rm -f campiello_mount.rsrc

# The native daemon. Its Daemon.rdef marks it B_BACKGROUND_APP (no Deskbar entry) and carries the
# app_signature so be_roster can find it.
campiello_daemon: $(DAEMON) $(SRV_DIR)/Daemon.rdef $(ICON_RDEF)
	$(CXX) $(CXXFLAGS) -o $@ $(DAEMON) -lbe -lssl -lcrypto -lnetwork
	rc -o campiello_daemon.rsrc $(SRV_DIR)/Daemon.rdef $(ICON_RDEF)
	xres -o $@ campiello_daemon.rsrc
	mimeset -f $@
	rm -f campiello_daemon.rsrc

campiello_net: $(NET_ADDON)
	$(CXX) $(CXXFLAGS) -shared -fPIC -D_FILE_OFFSET_BITS=64 -I$(FUSE_HEADERS) \
	    -o $@ $(NET_ADDON) -luserlandfs_fuse -lssl -lcrypto -lnetwork

# The Deskbar replicant is a shelf add-on (.so). Its app_signature resource (from the .rdef,
# embedded with rc/xres and registered with mimeset) lets the roster find it by signature so the
# Deskbar can reload it after a reboot, and so the daemon can locate it to install it.
campiello_replicant: $(REPLICANT) $(REPL_DIR)/PeerReplicant.rdef
	$(CXX) $(CXXFLAGS) -shared -fPIC -o $@ $(REPLICANT) -lbe -lnetwork
	rc -o campiello_replicant.rsrc $(REPL_DIR)/PeerReplicant.rdef
	xres -o $@ campiello_replicant.rsrc
	mimeset -f $@
	rm -f campiello_replicant.rsrc

# The WON app (World O' Networking; codename vicinato): its app_signature resource
# (mimeset-registered) lets be_roster find it by signature and launch the connect helpers.
campiello_won: $(VICINATO) $(VIC_DIR)/Vicinato.rdef $(ICON_RDEF)
	$(CXX) $(CXXFLAGS) -o $@ $(VICINATO) -lbe -lmedia -lnetwork -llocalestub
	rc -o campiello_won.rsrc $(VIC_DIR)/Vicinato.rdef $(ICON_RDEF)
	xres -o $@ campiello_won.rsrc
	mimeset -f $@
	rm -f campiello_won.rsrc

# Build the WON app's catalog from its checked-in catkeys.
%.catalog: $(VIC_DIR)/locales/%.catkeys
	linkcatkeys -l $* -s $(CATALOG_SIG) -o $@ $<

# Regenerate the catkeys template from the WON source (run after adding/changing B_TRANSLATE
# strings): preprocess in collecting mode (-P drops the line markers that break the key scanner),
# then collect. Merge new keys into the other locales/*.catkeys by hand.
.PHONY: catkeys
catkeys:
	$(CXX) -DB_COLLECTING_CATKEYS -E -P -x c++ -std=c++17 $(VIC_DIR)/campiello_vicinato.cpp > won.pre
	collectcatkeys -s $(CATALOG_SIG) -l it -o $(VIC_DIR)/locales/it.catkeys won.pre
	rm -f won.pre
	@echo "Refreshed src/vicinato/locales/it.catkeys; merge new keys into the other locales by hand."

clean:
	rm -rf $(STAGE) campiello_sftp campiello_mount campiello_daemon campiello_net \
	    campiello_replicant campiello_won $(CATALOGS) won.pre $(PKG)
