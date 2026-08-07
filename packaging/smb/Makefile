# Build the OPTIONAL Campiello SMB package (.hpkg).
#
# Ships the Windows shared-folder surface: the campiello_smb userlandfs add-on + the
# campiello_smb_mount connect helper. Kept separate from the MIT core package because it links
# libsmb2 (LGPL-2.1), loaded dynamically; the core campiello package stays libsmb2-free
# (docs/SMB.md, working agreement rule 2).
#
#   make package    # builds campiello_smb-<version>-<arch>.hpkg
#   pkgman install ./campiello_smb-0.1.0-1-x86_64.hpkg
#
# Haiku-only (needs the `package` tool, the userlandfs FUSE headers, and libsmb2).

VERSION := 0.2.0-15
ARCH := x86_64
PKG := campiello_smb-$(VERSION)-$(ARCH).hpkg

CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -Werror -O2
# NOTE: do NOT define B_USE_POSITIVE_POSIX_ERRORS here. libsmb2 is built with Haiku's native
# (negative) errno (recipe REVISION 6, native errno + sign-agnostic socket.c), and this flag would also change the errno the FUSE front
# end (CampielloFuse/FuseTranslate) returns to libuserlandfs_fuse, breaking the mount. Match the
# SFTP add-on, which builds without it.
FUSE_HEADERS := /boot/system/develop/headers/private/userlandfs/fuse

SMB := ../../optional/smb
FUSE_DIR := ../../src/fondamenta/fuse

# Localization (Haiku Locale Kit), for the campiello_smb_mount GUI helper only - the userlandfs
# add-on (campiello_smb) has no user-facing strings. Source strings are Italian and show when no
# catalog matches the user's language; the catalogs here translate them. CATALOG_DIR is the
# signature with the "application/" prefix dropped, which is where BLocaleRoster looks.
CATALOG_SIG := application/x-vnd.Campiello-smb-mount
CATALOG_DIR := x-vnd.Campiello-smb-mount
LANGS := en
CATALOGS := $(LANGS:%=%.catalog)

# The userlandfs add-on: hosts the SMB backends in the reused core FUSE front end.
ADDON := $(SMB)/campiello_smb_main.cpp $(SMB)/SmbMount.cpp $(SMB)/SmbBackend.cpp \
         $(SMB)/SmbServerBackend.cpp \
         $(FUSE_DIR)/CampielloFuse.cpp $(FUSE_DIR)/FuseTranslate.cpp
# The connect helper GUI.
HELPER := $(SMB)/campiello_smb_mount.cpp $(SMB)/SmbMount.cpp $(SMB)/SmbBackend.cpp

STAGE := stage

.PHONY: package clean
package: $(PKG)

$(PKG): campiello_smb.PackageInfo campiello_smb campiello_smb_mount $(CATALOGS)
	rm -rf $(STAGE)
	mkdir -p $(STAGE)/add-ons/userlandfs $(STAGE)/apps
	mkdir -p $(STAGE)/data/locale/catalogs/$(CATALOG_DIR)
	cp campiello_smb $(STAGE)/add-ons/userlandfs/campiello_smb
	cp campiello_smb_mount $(STAGE)/apps/campiello_smb_mount
	for l in $(LANGS); do cp $$l.catalog $(STAGE)/data/locale/catalogs/$(CATALOG_DIR)/$$l.catalog; done
	package create -C $(STAGE) -i campiello_smb.PackageInfo $(PKG)
	@echo "Built $(PKG)"

campiello_smb: $(ADDON)
	$(CXX) $(CXXFLAGS) -shared -fPIC -D_FILE_OFFSET_BITS=64 -I$(FUSE_HEADERS) \
	    -o $@ $(ADDON) -luserlandfs_fuse -lsmb2

# The SMB connect helper. Its app_signature resource (mimeset-registered) lets be_roster find it
# by signature so the Vicinato can open it on a double-click of a Windows share. liblocalestub so
# B_TRANSLATE resolves the app's catalog at runtime.
campiello_smb_mount: $(HELPER) $(SMB)/SmbMountApp.rdef
	$(CXX) $(CXXFLAGS) -o $@ $(HELPER) -lbe -lsmb2 -lnetwork -lcrypto -llocalestub
	rc -o campiello_smb_mount.rsrc $(SMB)/SmbMountApp.rdef
	xres -o $@ campiello_smb_mount.rsrc
	mimeset -f $@
	rm -f campiello_smb_mount.rsrc

# Build a catalog from its checked-in catkeys. To refresh the catkeys after changing strings, see
# `make catkeys` below.
%.catalog: $(SMB)/locales/%.catkeys
	linkcatkeys -l $* -s $(CATALOG_SIG) -o $@ $<

# Regenerate the catkeys template from the source (run after adding/changing B_TRANSLATE strings):
# preprocess in collecting mode (-P drops the line markers that break the key scanner), then collect.
# Only campiello_smb_mount.cpp carries user-facing strings; the userlandfs add-on has none.
.PHONY: catkeys
catkeys:
	$(CXX) -DB_COLLECTING_CATKEYS -E -P -x c++ -std=c++17 $(SMB)/campiello_smb_mount.cpp > smb.pre
	collectcatkeys -s $(CATALOG_SIG) -l it -o $(SMB)/locales/it.catkeys smb.pre
	rm -f smb.pre
	@echo "Refreshed locales/it.catkeys; merge new keys into the other locales/*.catkeys by hand."

clean:
	rm -rf $(STAGE) campiello_smb campiello_smb_mount $(CATALOGS) smb.pre $(PKG)
