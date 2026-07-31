# Build the userlandfs FUSE smoke-test filesystem.
#
# Produces a shared library that exports main() and links libuserlandfs_fuse.so, the
# form a userlandfs FUSE filesystem takes. Install and mount instructions are in README.md.

CC ?= gcc
FUSE_HEADERS := /boot/system/develop/headers/private/userlandfs/fuse
# FUSE requires 64-bit file offsets (enforced by fuse_common.h).
CFLAGS ?= -Wall -Wextra -O2 -fPIC -D_FILE_OFFSET_BITS=64 -I$(FUSE_HEADERS)
LDFLAGS ?= -shared
LDLIBS ?= -luserlandfs_fuse

TARGETS := campiello_smoke campiello_passthrough

all: $(TARGETS)

campiello_smoke: campiello_smoke.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ campiello_smoke.c $(LDLIBS)

campiello_passthrough: campiello_passthrough.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ campiello_passthrough.c $(LDLIBS)

.PHONY: all clean
clean:
	rm -f $(TARGETS)
