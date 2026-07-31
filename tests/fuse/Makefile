# Build and run the Fondamenta FUSE front-end tests.
#
# The translation layer (FuseTranslate) and the mount-arg parser (SftpMount) are pure standard
# C++ and run anywhere. On Haiku we also link the full SFTP userlandfs add-on (CampielloFuse +
# the backend + libssh2 + libuserlandfs_fuse) to verify the whole thing compiles and resolves;
# it is not mounted here (that is a manual test on real hardware). On non-Haiku the add-on is
# skipped.

CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -Werror -O2

FUSE_DIR := ../../src/fondamenta/fuse
BE_DIR := ../../src/fondamenta/backend
FUSE_HEADERS := /boot/system/develop/headers/private/userlandfs/fuse

TESTS := test_fusetranslate test_sftpmount

test_fusetranslate: test_fusetranslate.cpp $(FUSE_DIR)/FuseTranslate.cpp $(FUSE_DIR)/FuseTranslate.h
	$(CXX) $(CXXFLAGS) -o $@ test_fusetranslate.cpp $(FUSE_DIR)/FuseTranslate.cpp

test_sftpmount: test_sftpmount.cpp $(FUSE_DIR)/SftpMount.cpp $(FUSE_DIR)/SftpMount.h
	$(CXX) $(CXXFLAGS) -o $@ test_sftpmount.cpp $(FUSE_DIR)/SftpMount.cpp

# The full userlandfs SFTP add-on: exports main(), links libuserlandfs_fuse.so and libssh2.
ADDON_SRCS := $(FUSE_DIR)/campiello_sftp_main.cpp $(FUSE_DIR)/CampielloFuse.cpp \
              $(FUSE_DIR)/FuseTranslate.cpp $(FUSE_DIR)/SftpMount.cpp \
              $(BE_DIR)/SftpBackend.cpp $(BE_DIR)/SftpKnownHosts.cpp

MOUNT_DIR := ../../src/fondamenta/mount
HELPER_SRCS := $(MOUNT_DIR)/campiello_mount.cpp $(FUSE_DIR)/SftpMount.cpp \
               $(BE_DIR)/SftpBackend.cpp $(BE_DIR)/SftpKnownHosts.cpp

.PHONY: check clean addon helper
ifeq ($(shell uname),Haiku)
check: $(TESTS) addon helper
	@for t in $(TESTS); do echo "== $$t ==" && ./$$t || exit 1; done

addon: $(ADDON_SRCS)
	$(CXX) $(CXXFLAGS) -shared -fPIC -D_FILE_OFFSET_BITS=64 -I$(FUSE_HEADERS) \
	    -o campiello_sftp $(ADDON_SRCS) -luserlandfs_fuse -lssh2
	@echo "== campiello_sftp == add-on linked (not mounted)"

# The connect helper is a libbe GUI app; link it to verify it builds (not run here).
helper: $(HELPER_SRCS)
	$(CXX) $(CXXFLAGS) -o campiello_mount $(HELPER_SRCS) -lbe -lssh2 -lnetwork
	@echo "== campiello_mount == helper linked (not run)"
else
check: $(TESTS)
	@for t in $(TESTS); do echo "== $$t ==" && ./$$t || exit 1; done
	@echo "== campiello_sftp / campiello_mount == skipped (not Haiku)"
endif

clean:
	rm -f test_fusetranslate test_sftpmount campiello_fuse.o campiello_sftp campiello_mount
