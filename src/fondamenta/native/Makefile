# Build the native userlandfs filesystem (fs_interface front end).
#
# Produces a shared library that exports a `modules` array and links
# libuserlandfs_haiku_kernel.so (which provides userlandfs_create_file_system and the
# publish_vnode/get_vnode helpers). Install and mount like the FUSE smoke tests; see
# ../smoke/README.md and README.md here.

CC ?= gcc
CFLAGS ?= -Wall -Wextra -O2 -fPIC
LDFLAGS ?= -shared
LDLIBS ?= -luserlandfs_haiku_kernel

TARGET := campiello_native

$(TARGET): campiello_native.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ campiello_native.c $(LDLIBS)

.PHONY: clean
clean:
	rm -f $(TARGET)
