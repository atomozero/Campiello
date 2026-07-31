# Build and run the SMB backend tests, and (on Haiku) the SMB userlandfs add-on.
#
# This module is OPTIONAL and links libsmb2 (LGPL-2.1), so it is kept out of the core `make
# check`. Our own code (SmbBackend, SmbMount, campiello_smb) stays MIT and links libsmb2.so
# dynamically (docs/SMB.md). The integration test SKIPs at run time without a configured server,
# so `make check` here is green offline; the mount-argument parser test always runs.

CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -Werror -O2
# Do NOT define B_USE_POSITIVE_POSIX_ERRORS: libsmb2 (HaikuPorts REVISION 6, native errno +
# sign-agnostic socket.c) uses Haiku's native (negative) errno, so SmbBackend interprets its -errno
# returns with the native constants, and the FUSE add-on's front end keeps the errno semantics
# libuserlandfs_fuse expects. See docs/SMB.md.
LDLIBS ?= -lsmb2
ifeq ($(shell uname),Haiku)
LDLIBS += -lnetwork
endif

# The userlandfs FUSE add-on (Haiku-only): reuses the core FUSE front end and hosts the SMB backends.
FUSE_HEADERS := /boot/system/develop/headers/private/userlandfs/fuse
FUSE_DIR := ../../src/fondamenta/fuse
SMB_ADDON := campiello_smb_main.cpp SmbMount.cpp SmbBackend.cpp SmbServerBackend.cpp \
             $(FUSE_DIR)/CampielloFuse.cpp $(FUSE_DIR)/FuseTranslate.cpp

TESTS := test_smbmount test_smbbackend test_smbserver

test_smbmount: test_smbmount.cpp SmbMount.cpp SmbBackend.cpp SmbMount.h SmbBackend.h
	$(CXX) $(CXXFLAGS) -o $@ test_smbmount.cpp SmbMount.cpp SmbBackend.cpp $(LDLIBS)

test_smbbackend: test_smbbackend.cpp SmbBackend.cpp SmbBackend.h
	$(CXX) $(CXXFLAGS) -o $@ test_smbbackend.cpp SmbBackend.cpp $(LDLIBS)

test_smbserver: test_smbserver.cpp SmbServerBackend.cpp SmbBackend.cpp SmbServerBackend.h SmbBackend.h
	$(CXX) $(CXXFLAGS) -o $@ test_smbserver.cpp SmbServerBackend.cpp SmbBackend.cpp $(LDLIBS)

# The FUSE add-on: a shared library exporting main(), links libuserlandfs_fuse and libsmb2.
campiello_smb: $(SMB_ADDON)
	$(CXX) $(CXXFLAGS) -shared -fPIC -D_FILE_OFFSET_BITS=64 -I$(FUSE_HEADERS) \
	    -o $@ $(SMB_ADDON) -luserlandfs_fuse -lsmb2

# The connect helper (Haiku-only GUI): validates the login with libsmb2, then mounts.
HELPER := campiello_smb_mount.cpp SmbMount.cpp SmbBackend.cpp
campiello_smb_mount: $(HELPER)
	$(CXX) $(CXXFLAGS) -o $@ $(HELPER) -lbe -lsmb2 -lnetwork -lcrypto

.PHONY: check clean
check: $(TESTS)
	@for t in $(TESTS); do echo "== $$t =="; ./$$t || exit 1; done

clean:
	rm -f $(TESTS) campiello_smb
