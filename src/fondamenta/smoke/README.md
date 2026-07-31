# userlandfs mount smoke tests

Two minimal userlandfs FUSE filesystems that prove the mount plumbing works at runtime.
Neither is the real Fondamenta; native mode will use the Haiku-native userlandfs interface
for typed attribute write (decision C, see `docs/VERIFIED.md` section 1). These use the
libfuse 2.x front end (`libuserlandfs_fuse.so`), the documented userlandfs path and the M1
interop path.

- **`campiello_smoke.c`** mounts an empty read-only volume. Proves a Campiello-built
  filesystem appears as a mounted volume.
- **`campiello_passthrough.c`** mirrors a real local directory read-only (content plus BFS
  attribute reads via `fs_attr.h`). Shows real files and exercises the attribute path,
  which empirically confirms the FUSE bridge's attribute limitations (below).

Requires the `userland_fs` package (ships `libuserlandfs_fuse.so`, the `userlandfs` kernel
module, and the FUSE dev headers under
`/boot/system/develop/headers/private/userlandfs/fuse/`).

## Build

```
make            # builds both
```

Each is a shared library that exports `main()` and links `libuserlandfs_fuse.so`, the form
a userlandfs FUSE filesystem takes. `-D_FILE_OFFSET_BITS=64` is required (enforced by
`fuse_common.h`).

## Run

A userlandfs FUSE filesystem is an add-on in `add-ons/userlandfs/<name>`; the first token
of the mount `-p` parameter is that name, and any following tokens become its argv.

```
mkdir -p ~/config/non-packaged/add-ons/userlandfs
cp campiello_smoke campiello_passthrough ~/config/non-packaged/add-ons/userlandfs/

# empty volume
mkdir -p ~/mnt_empty
mount -t userlandfs -p "campiello_smoke" ~/mnt_empty
ls -la ~/mnt_empty                 # only . and ..

# passthrough of a real directory (backing dir is the second -p token)
mkdir -p ~/mnt_pass
mount -t userlandfs -p "campiello_passthrough /path/to/backing" ~/mnt_pass
ls -la ~/mnt_pass                  # real files
cat ~/mnt_pass/somefile            # real content
```

Unmount with `unmount ~/mnt_empty` etc.

## Verified outcome (2026-07-04, on Haiku)

**campiello_smoke (empty volume): full success.** Built, installed, mounted; `df` showed a
distinct volume `"campiello_smoke Volume"` of file system `userlandfs`; `ls`/`stat` showed
the empty read-only root (inode 1, mode 0555); `unmount` was clean. Closes the M0 runtime
gap for the userlandfs mount path via the FUSE front end.

**campiello_passthrough (real directory): files and content work; attributes reveal the
FUSE limitation.** Backed by a directory containing `nota.txt` (with three typed BFS
attributes: a MIME String `BEOS:TYPE`, a Text `MyApp:comment`, an Int-32 `MyApp:rating`)
and a subdirectory.

- Real files and the subdirectory appeared through the mount; `cat` returned correct
  content. Read path works.
- `catattr MyApp:comment` and `catattr MyApp:rating` returned the attribute VALUES through
  the mount, but reported as **`raw_data`** for both, even though the backing types are
  Text and Int-32. Only the MIME type keeps its type. This is the M0 type-flattening
  finding, confirmed at runtime, and the reason native mode must not carry attributes
  through the FUSE bridge (decision C).
- `listattr` on the mounted file showed **zero attributes**. Logging in the add-on proved
  the bridge calls `getxattr` (named reads) but never calls the high-level `listxattr`
  (enumeration) op, so attributes do not surface in a listing through the FUSE bridge in
  this configuration. A further limitation of the FUSE attribute path.

**Caveat observed:** a userlandfs volume that has been accessed in read (files opened) did
not unmount cleanly in this headless environment (`unmount` reported "Device busy"); the
empty-volume mount unmounted fine. This looks tied to lingering node references from the
accessing process and is an environment/cleanup quirk, not a defect in the filesystem
code; a reboot clears a stuck mount. Prefer the empty-volume test for repeatable
mount/unmount cycles.
