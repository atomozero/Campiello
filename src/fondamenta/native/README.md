# Native userlandfs front end (fs_interface)

`campiello_native.c` is a minimal in-memory, read-only userlandfs filesystem written
against the Haiku-native `fs_interface` (`file_system_module_info` / `fs_vnode_ops`), loaded
via `libuserlandfs_haiku_kernel.so`. This is the front end **native mode** uses (decision C,
`docs/VERIFIED.md` section 1), in contrast to the FUSE bridge in `../smoke/`.

Purpose: prove that the native front end carries BFS attribute **types both ways**, which
the FUSE bridge cannot. The volume exposes one file, `nota.txt`, seeded with a
`B_STRING_TYPE` and a `B_INT32_TYPE` attribute, and it accepts attribute **writes**
(`create_attr` / `write_attr` / `remove_attr`) into a mutable in-memory store, so a typed
`addattr` round-trips.

Not the real Fondamenta: it is in memory, single-file, and non-persistent (file content is
read-only; only attributes are writable). It exists to validate the native path at runtime.

## How a native userlandfs filesystem is structured (verified)

Unlike the FUSE add-on (which exports `main`), a native one:

- exports a `modules` array (`module_info**`), the standard Haiku kernel-module mechanism,
  containing a `file_system_module_info` named `file_systems/<name>/v1`;
- links `libuserlandfs_haiku_kernel.so`, which provides `userlandfs_create_file_system`
  (it looks up `modules` and the `file_systems/<name>/v1` entry) plus the `publish_vnode` /
  `get_vnode` / `put_vnode` helpers the module calls.

Verified against `server/haiku/HaikuKernelFileSystem.cpp:385` and the in-tree `bindfs`
example (`mount` publishes the root via `publish_vnode`; `lookup` acquires a reference via
`get_vnode`; `get_vnode` sets `fs_vnode.ops`).

## Build, install, mount

```
make
mkdir -p ~/config/non-packaged/add-ons/userlandfs
cp campiello_native ~/config/non-packaged/add-ons/userlandfs/
mkdir -p ~/cnative_mnt
mount -t userlandfs -p "campiello_native" ~/cnative_mnt
```

## Verify (the proof)

```
listattr -l ~/cnative_mnt/nota.txt          # seeded Text and Int-32 attributes
catattr MyApp:comment ~/cnative_mnt/nota.txt
# write round-trip: create typed attributes and read them back typed
addattr -t int32  MyApp:score 99      ~/cnative_mnt/nota.txt
addattr -t string MyApp:tag   prova   ~/cnative_mnt/nota.txt
listattr -l ~/cnative_mnt/nota.txt          # MyApp:score as Int-32, MyApp:tag as Text
rmattr MyApp:score ~/cnative_mnt/nota.txt
unmount ~/cnative_mnt
```

## Verified outcome (2026-07-04, on Haiku)

Built, installed, mounted; `nota.txt` appeared with correct content. The decisive result,
`listattr -l`:

```
        Type       Size  Name                                Contents
        Text        15  "MyApp:comment"                     ciao tipizzato
      Int-32         4  "MyApp:rating"                      5
```

`catattr` reported `MyApp:comment` as `string` and `MyApp:rating` as `int32`. The native
front end **preserves attribute types and enumerates attributes**, exactly what the FUSE
bridge could not do (there both surfaced as `raw_data` and `listattr` showed nothing, see
`../smoke/README.md`).

The **write round-trip** also succeeded: `addattr -t int32 MyApp:score 99` and
`addattr -t string MyApp:tag prova` created typed attributes via `create_attr`/`write_attr`;
`listattr -l` then showed `MyApp:score` as `Int-32` (99) and `MyApp:tag` as `Text` (prova),
and `catattr` read them back as `int32` / `string`. `rmattr MyApp:score` removed it. So the
native front end carries attribute types in BOTH directions. That result stands: the typed
read and write were observed directly.

## HAZARD: unmount is NOT clean, it can panic the kernel

An earlier version of this file claimed the volume "unmounted cleanly". That was wrong and
is corrected here. In two separate runs, tearing down a userlandfs volume that this module
had served led to a **kernel panic** (`ASSERT FAILED ... vfs.cpp: vnodes.IsEmpty()`) during
`unmount`, dropping the machine into KDL and requiring a reboot. The `unmount` command
returned success while the `userlandfs_server` process lingered, i.e. the teardown was not
actually complete; a later teardown tripped the assertion.

A vnode is left referenced in the real kernel's vnode list at unmount. Investigation from
source (2026-07-04) showed `HaikuKernelVolume::Lookup`/`Unmount`/`PutVNode` pass straight
through to this module's ops without adding references, so the naive "double get_vnode in
lookup" theory does NOT hold. The exact cause (a server-to-kernel vnode refcount subtlety in
userlandfs, or a missing behavior in this module) is undetermined; pinning it down needs a
throwaway VM to iterate, and the KDL `vnodes` dump of the stuck mount.

Do **not** mount this on a machine you cannot afford to reboot. The attribute-type result
above is real and was observed before the panic; the clean-shutdown claim is retracted.
