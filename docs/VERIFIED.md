# Verified ground truth: facts checked against real source

Facts checked against real source, with the paths that were read. This file is
authoritative: when a verified fact changes, update it in the same commit that depends
on the change (working agreement rule 6). Never re-derive these from memory.

Two provenance classes:

- **Haiku source** facts verified 2026-07-03 against the tree at
  `/boot/home/Desktop/haiku-build`, and HaikuPorts recipes at
  `/boot/home/Desktop/haikuports`. Citations are `path:line` relative to those roots.
- **Reference-project** facts verified 2026-07-03 by reading the sibling projects under
  `/Magazzino/`. Full harvest in `docs/REUSE.md`.

M0 status: the verification spike is **substantially complete**. Every open question
from PROPOSAL.md section 20 that can be answered by reading source has been answered
below. Remaining items need a live two-node run or the `userland_fs` package installed
(flagged PARTIAL). One finding (FUSE attribute path is read-only + type-lossy) has an
architecture implication tracked under Risks and decisions.

---

## 1. userlandfs and the FUSE bridge

Root: `src/add-ons/kernel/file_systems/userlandfs/`. Contains `Jamfile`,
`kernel_add_on/`, `private/`, `server/`, `shared/`. FUSE bridge at `server/fuse/`.

- **API level is libfuse 2.9, high-level `struct fuse_operations` (NOT FUSE 3.x).**
  `headers/private/userlandfs/fuse/fuse_common.h:23,26` define `FUSE_MAJOR_VERSION 2`,
  `FUSE_MINOR_VERSION 9`. `struct fuse_operations` at
  `headers/private/userlandfs/fuse/fuse.h:89`. Jamfile builds `PACKAGE_VERSION=2.9.9`
  (`server/fuse/Jamfile:23`). CONFIRMED.
- **Default `FUSE_USE_VERSION` is 21** (`fuse.h:22-23`) if the client does not define it.
  This is very old; Fondamenta must `#define FUSE_USE_VERSION 26` before including
  `fuse.h` to get the intended 2.6+ `fuse_operations` layout. CONFIRMED.
- **xattr callbacks exist and are unguarded** (Linux-style, no macOS `position` arg):
  `setxattr` 5-arg (`fuse.h:261`), `getxattr` 4-arg (`:264`), `listxattr` (`:267`),
  `removexattr` (`:270`). Phase-1 read callbacks: `getattr` (`:96`), `readlink` (`:106`),
  `open` (`:174`), `read` (`:187`), `statfs` (`:208`), `release` (`:249`), `readdir`
  (`:305`). CONFIRMED.

### The xattr mapping (the load-bearing finding)

The bridge direction is the reverse of the naive assumption. `userlandfs` presents a
**Haiku-native filesystem** to the VFS; `FUSEVolume` (`server/fuse/FUSEVolume.cpp`)
translates the Haiku attribute hooks it receives into **FUSE xattr calls** on the loaded
FUSE add-on via `fuse_fs_*` / `fuse_ll_*` helpers. It never calls `fs_attr.h` or `BNode`.

- Haiku attr-dir listing -> FUSE `listxattr` (`FUSEVolume.cpp:2418,2420,2434,2437`).
- Haiku open/read attr -> FUSE `getxattr` (`FUSEVolume.cpp:2505,2516,2545,2547`).
- **Attribute WRITE is not implemented.** `FUSEVolume::OpenAttr` returns `B_UNSUPPORTED`
  for any `openMode != O_RDONLY` (`FUSEVolume.cpp:2495-2497`). There is no `WriteAttr`
  or `RemoveAttr` on `FUSEVolume`; `fuse_fs_setxattr`/`removexattr` are never called.
  CONFIRMED.
- **Type mapping is lossy.** The MIME attribute is synthesized to `B_MIME_STRING_TYPE`,
  every other attribute surfaces as `B_RAW_TYPE` (`FUSEVolume.cpp:355-359`), reported via
  `st->st_type` in `ReadAttrStat` (`:2603`). A `BEOS:TYPE` MIME attr is fabricated from
  the file extension when absent (`:2520-2527`). CONFIRMED.

Implication tracked under Risks and decisions (item 1).

**Implementation status (2026-07-21).** `CampielloFuse` now wires the attribute READ path that the
bridge actually uses: `getxattr` and `listxattr` are served from the CNP `READ_ATTRS` reply
(whole-node `AttrSet`), so a peer's BFS attribute names and values are readable through a mounted
Campiello volume. `setxattr` is wired too but is inert on Haiku (the bridge never calls it, per the
CONFIRMED finding above); typed attribute WRITE to a peer is the `WRITE_ATTRS` wire op (implemented
in `FileServer::HandleWriteAttrs` and `CnpBackend::WriteAttrs`, covered by `test_fileserver` /
`test_cnpbackend`), driven by the native userlandfs path (decision C), not this FUSE bridge. The
file-write path (create/write/mkdir/unlink/rename/truncate) is wired end to end and tested; the only
part still needing a live mounted round-trip (throwaway VM) is confirming attribute read fidelity
through the real bridge.

### Build path

- FUSE add-ons link `libuserlandfs_fuse.so` (`server/fuse/Jamfile:26,39`) and call
  `fuse_main()` (`server/fuse/fuse_main.cpp:21`) - the standard model used by the sshfs
  and encfs HaikuPorts. CONFIRMED.
- Dev headers ship out-of-tree at
  `/boot/system/develop/headers/private/userlandfs/{fuse,legacy,private,shared}`, so
  out-of-tree compilation is possible on this system. CONFIRMED.
- **The `userland_fs` package is now installed and the mount path is runtime-verified
  (2026-07-04).** The package ships `libuserlandfs_fuse.so`, `libuserlandfs_haiku_kernel.so`,
  `libuserlandfs_beos_kernel.so`, the `userlandfs` kernel fs module, and the FUSE dev
  headers under `/boot/system/develop/headers/private/userlandfs/fuse/`. A minimal FUSE
  add-on (`src/fondamenta/smoke/`) built against those headers (needs
  `-D_FILE_OFFSET_BITS=64`, enforced by `fuse_common.h`), linked `libuserlandfs_fuse.so`,
  installed to `B_USER_NONPACKAGED_ADDONS_DIRECTORY/userlandfs/`, and mounted with
  `mount -t userlandfs -p "<name>" <dir>` (no device). `df` reported a distinct volume of
  file system `userlandfs`; `ls`/`stat` showed the empty read-only root; `unmount` was
  clean. CONFIRMED for the FUSE front end. The native `fs_interface` front end (decision C)
  is still only source-verified, not yet runtime-tested.

### The Haiku-native userlandfs interface (used by native mode, decision C)

Besides the FUSE bridge, userlandfs exposes a native interface an implementer fills.
This is the path native mode uses to write typed BFS attributes (which the FUSE bridge
cannot). Two concrete ways to implement it:

- **C++ base classes** `UserlandFS::FileSystem` (`server/FileSystem.h:24`, factory
  `CreateVolume`/`DeleteVolume` at `:33-34`) and `UserlandFS::Volume` (`server/Volume.h:23`,
  ~90 virtuals covering the full vnode/attr/index/query surface). All three in-tree front
  ends subclass this pair (`FUSEVolume`, `HaikuKernelVolume`, `BeOSKernelVolume`). CAVEAT:
  these base-class headers (`FileSystem.h`, `Volume.h`, `IORequestInfo.h`,
  `RequestThread.h`) are **NOT shipped in the SDK** - the installed
  `headers/private/userlandfs/` has only `private/`, `shared/`, `fuse/`, `legacy/`. Using
  them means vendoring private, unversioned headers. CONFIRMED.
- **Public module interface (preferred)**: write a standard Haiku
  `file_system_module_info` / `fs_vnode_ops` against the **public, shipped** header
  `headers/os/drivers/fs_interface.h`, and load it through the already-installed
  `libuserlandfs_haiku_kernel.so` (the `HaikuKernelVolume` adapter forwards each `Volume`
  method to the module's `ops->*` pointer). This gets typed attribute write without any
  private-header dependency. CONFIRMED.

**Typed attribute write is real here** (the decisive fact for decision C). The type_code
is fixed at attribute creation and preserved end to end:
- native C++ `CreateAttr(void* node, const char* name, uint32 type, int openMode,
  void** cookie)` (`server/Volume.h:138-140`), with `WriteAttr` (`:148-150`), `ReadAttr`
  (`:145-147`), `RemoveAttr` (`:158`), attr-dir `OpenAttrDir`/`ReadAttrDir`/... (`:128-135`).
- carried on the kernel<->userland RPC: `CreateAttrRequest { ...; uint32 type; ... }`
  (`headers/private/userlandfs/private/Requests.h:1162-1164`).
- matches the public module op `create_attr(fs_volume*, fs_vnode*, const char* name,
  uint32 type, int openMode, void** _cookie)` (`headers/os/drivers/fs_interface.h:217-219`).
  (Haiku semantics: type is set at create; `write_attr` streams bytes with no type param.)
CONFIRMED.

Registration/mount is the same as FUSE: export
`extern "C" status_t userlandfs_create_file_system(const char* fsName, image_id image,
FileSystem** _fileSystem)` (`server/FileSystem.h:68-69`); the server finds the add-on via
`BPathFinder`(`B_FIND_PATH_ADD_ONS_DIRECTORY, "userlandfs", ...)` +
`get_image_symbol_etc(..., recursive=true)` (`server/UserlandFSServer.cpp:72-114`), so the
symbol may come from a linked `libuserlandfs_*.so`. Campiello does not write its own
`main()` (the generic dispatcher is `server/main.cpp:47-117`). CONFIRMED.

Build: a native fs is a `SharedLibrary` linking `<nogrist>userlandfs_server` (NOT
`libuserlandfs_fuse.so`), mirroring `server/haiku/Jamfile:24-55`. The
public-module-interface variant instead ships a plain fs module and reuses the installed
`libuserlandfs_haiku_kernel.so`. CONFIRMED.

**Runtime-verified (2026-07-04) via the public-module-interface variant.** A minimal
in-memory native module (`src/fondamenta/native/campiello_native.c`) that exports a
`modules` array with a `file_system_module_info` named `file_systems/campiello_native/v1`
and links `libuserlandfs_haiku_kernel.so` built, mounted, and served a file with typed
attributes (read and write). The registration mechanism was confirmed live:
`userlandfs_create_file_system` finds `modules` via `get_image_symbol` and matches
`file_systems/<name>/v1` (`server/haiku/HaikuKernelFileSystem.cpp:385`);
`libuserlandfs_haiku_kernel.so` exports the `publish_vnode`/`get_vnode`/`put_vnode` helpers
the module calls. The bindfs conventions (`mount` publishes root, `lookup` acquires via
`get_vnode`) held for the read/write path.

**HAZARD (corrects an earlier claim of clean unmount):** tearing down a volume this module
served twice caused a kernel panic `ASSERT FAILED ... vfs.cpp: vnodes.IsEmpty()` during
`unmount` (KDL, reboot required). `unmount` returned success while `userlandfs_server`
lingered, so the teardown was incomplete. Source review showed the adapter
(`HaikuKernelVolume::Lookup`/`Unmount`/`PutVNode`) passes straight through to the module
ops without adding references, so the "double get_vnode in lookup" theory does NOT hold; the
real cause (a userlandfs server-to-kernel vnode refcount subtlety, or a missing module
behavior) is undetermined and needs a throwaway VM plus the KDL `vnodes` dump to pin down.
The attribute-type results are real (observed before the panic); the front end is proven for
attribute fidelity but its unmount is NOT clean. Do not mount on non-sacrificial hardware.

## 2. Mounting a volume into Tracker

Public C API (libroot), no private syscall needed:
- `dev_t fs_mount_volume(const char* where, const char* device, const char* filesystem,
  uint32 flags, const char* parameters)` (`headers/os/kernel/fs_volume.h:25-27`).
- `status_t fs_unmount_volume(const char* path, uint32 flags)` (`:28`).
- Flags: `B_MOUNT_READ_ONLY=1`, `B_MOUNT_VIRTUAL_DEVICE=2` (`:14-15`),
  `B_FORCE_UNMOUNT=1` (`:18`). CONFIRMED.
- Mount with `filesystem = "userlandfs"` (the kernel client short name registered at
  `kernel_add_on/kernel_interface.cpp:1130`); the **first whitespace token of
  `parameters` is the specific userland fs name**, remainder passed to it
  (`kernel_interface.cpp:72-102,24-68`). CONFIRMED from source (not a public-header
  contract; re-verify against the installed userlandfs at M1).
- Tracker shows the mount automatically via node monitoring; there is no `BRoster::Mount`.

## 3. BFS attributes: native API and type codes

`fs_attr.h` lives at `headers/os/kernel/fs_attr.h` (NOT under `os/storage/`).

- `attr_info { uint32 type; off_t size; }` (`fs_attr.h:13-16`). type is a 32-bit
  type_code; size is 64-bit `off_t`.
- C API, all carry `uint32 type`: `fs_read_attr` (`:23`), `fs_write_attr` (`:25`),
  `fs_remove_attr` (`:27`), `fs_stat_attr` (`:28`), attr-dir
  `fs_open_attr_dir`/`fs_read_attr_dir`/`fs_close_attr_dir`/`fs_rewind_attr_dir`
  (`:37,41,40,42`). CONFIRMED.
- C++ `BNode` (`headers/os/storage/Node.h`), all use `type_code type`:
  `WriteAttr` (`:58`), `ReadAttr` (`:61`), `RemoveAttr` (`:64`), `RenameAttr` (`:65`),
  `GetAttrInfo` (`:67`), `GetNextAttrName` (`:69`), `RewindAttrs` (`:70`),
  `WriteAttrString` (`:71`), `ReadAttrString` (`:73`). Iterate with `RewindAttrs()` then
  `GetNextAttrName()` (returns `B_ENTRY_NOT_FOUND` at end), sizing the name buffer to
  `B_ATTR_NAME_LENGTH`. CONFIRMED.

Type codes (`headers/os/support/TypeConstants.h:15-70`), FourCC char-literals:

| Constant | Value | Line |
| --- | --- | --- |
| `B_BOOL_TYPE` | `'BOOL'` | 21 |
| `B_DOUBLE_TYPE` | `'DBLE'` | 24 |
| `B_FLOAT_TYPE` | `'FLOT'` | 25 |
| `B_INT16_TYPE` | `'SHRT'` | 27 |
| `B_INT32_TYPE` | `'LONG'` | 28 |
| `B_INT64_TYPE` | `'LLNG'` | 29 |
| `B_INT8_TYPE` | `'BYTE'` | 30 |
| `B_MESSAGE_TYPE` | `'MSGG'` | 35 |
| `B_MIME_TYPE` | `'MIME'` | 37 |
| `B_RAW_TYPE` | `'RAWT'` | 46 |
| `B_STRING_TYPE` | `'CSTR'` | 55 |
| `B_TIME_TYPE` | `'TIME'` | 57 |
| `B_UINT32_TYPE` | `'ULNG'` | 59 |
| `B_UINT64_TYPE` | `'ULLG'` | 60 |
| `B_MIME_STRING_TYPE` | `'MIMS'` | 65 |

Surprises: `B_STRING_TYPE` is `'CSTR'`, not `'TEXT'` (`'TEXT'` is the deprecated
`B_ASCII_TYPE`, `:68`, do not use). `B_MIME_STRING_TYPE` (`'MIMS'`) is distinct from
`B_MIME_TYPE` (`'MIME'`). Because these are char literals, Campiello must fix a wire
endianness for the 4 bytes (recommend the 4 ASCII chars in header order). CONFIRMED.

## 4. Queries and live queries

`headers/os/storage/Query.h`.

- `query_op` enum (`:25-40`): `B_EQ=1`..`B_NE=6`, `B_CONTAINS=7`, `B_BEGINS_WITH=8`,
  `B_ENDS_WITH=9`, `B_AND=0x101`, `B_OR=0x102`, `B_NOT=0x103`. CONFIRMED.
- Build a query two ways: high-level `SetPredicate(const char*)` (`:64`), or the RPN
  builder `PushAttr`/`PushOp`/`PushString`/`PushInt32`/... (`:50-61`). Then `SetVolume`
  (`:63`) and `Fetch()` (`:75`); enumerate with `GetNextEntry`/`GetNextRef`/
  `GetNextDirents` (`:78-81`). `GetPredicate` (`:69-70`) reads the textual predicate back.
  CONFIRMED.
- **Live query mechanism**: `BQuery::SetTarget(BMessenger)` (`:65`) makes the query live;
  `IsLive()` (`:67`). Updates arrive as BMessages with `what = B_QUERY_UPDATE = 'QUPD'`
  (`headers/os/app/AppDefs.h:120`), carrying an `opcode` field of `B_ENTRY_CREATED=1` /
  `B_ENTRY_REMOVED=2` (`headers/os/storage/NodeMonitor.h:36-37`). Kernel primitive
  `fs_open_live_query(dev, query, flags, port_id, token)`
  (`headers/os/kernel/fs_query.h:29-30`), flag `B_LIVE_QUERY=0x00000001` (`:15`).
  CONFIRMED.
- **Surprises to record so nobody hunts for a nonexistent API:**
  1. `BQuery::SetTarget` has ONLY a `BMessenger` overload - there is no
     `SetTarget(BHandler*)`. The `BHandler*`/`BLooper*` forms exist only on
     `watch_node`/`watch_volume` in NodeMonitor.h.
  2. The update message field keys - `"opcode"`, `"device"`, `"directory"`, `"node"`,
     `"name"` - are documented string literals (`NodeMonitor.h:31-34`), NOT named
     constants anywhere. Hardcode the strings.
  3. A live `B_QUERY_UPDATE` carries only a node identity (`device`+`directory`+`node`)
     plus the leaf `name` - NO full path and NO attribute values. To stream the entry and
     its attributes to a peer, the receiver must resolve the `entry_ref` and read
     attributes itself.

## 5. Volume enumeration and capability gates

- `BVolumeRoster::GetNextVolume(BVolume*)` / `Rewind()`
  (`headers/os/storage/VolumeRoster.h:23-24`); `StartWatching(BMessenger)` (`:28`).
- `BVolume` capability checks: `KnowsQuery()`, `KnowsAttr()`, `KnowsMime()`
  (`headers/os/storage/Volume.h:51-53`); plus `IsReadOnly()`, `IsRemovable()`,
  `IsPersistent()`, `IsShared()`, `Device()`, `GetName()`. CONFIRMED.
- Gate distributed queries on `KnowsQuery()` and attribute carriage on `KnowsAttr()`
  (BFS returns true; FAT and similar return false).

## 6. TLS backend (open question #4, RESOLVED)

- **`BSecureSocket` is OpenSSL-backed** (`src/kits/network/libnetapi/SecureSocket.cpp:12-16`
  includes `<openssl/ssl.h>`), but its **public API cannot serve Traghetto**:
  - No client-certificate loading -> no mutual auth. The shared client `SSL_CTX` never
    calls `SSL_CTX_use_certificate`/`use_PrivateKey` (whole file; context built at
    `:295-352` with `SSLv23_method()` + `SSL_OP_NO_SSLv2|NO_SSLv3` only).
  - No TLS-version control (`SecureSocket.cpp:300-308`).
  - No usable pinning: the only hook `CertificateVerificationFailed(BCertificate&, const
    char*)` (`headers/os/net/SecureSocket.h:23-24`) fires ONLY when OpenSSL chain
    validation already failed (`SecureSocket.cpp:228-229`, `if (ok) return ok;`), and
    `BCertificate` exposes no public-key / SPKI / fingerprint accessor - only
    `Issuer`/`Subject`/`String`/dates/`operator==` (`headers/os/net/Certificate.h:18-32`,
    `operator==` is a full-DER `X509_cmp`). The `SSL*`/`SSL_CTX*` are opaque
    (`SecureSocket.h:51-52`), so you cannot go under it.
  CONFIRMED. **Decision: do not use BSecureSocket for Traghetto; drive a TLS library
  directly.**
- System OpenSSL is **3.5.6, Apache-2.0** (`/boot/system/develop/headers/openssl/opensslv.h:31-37,90,110,7`).
  Core-legal. No `openssl/` headers vendored inside the Haiku source tree (external dep,
  gated by `OPENSSL_ENABLED`). CONFIRMED.
- **TLS backend decision**: mutual-auth + SPKI pinning via a directly-driven library.
  Primary: **system OpenSSL 3.x** (Apache-2.0, already linked by BSecureSocket and by the
  sibling LocalSend). Self-contained alternative: **bundle mbedTLS 3.6.5** (Apache-2.0).
  Both core-legal; both do TLS 1.3 + client-cert mutual auth + public-key pinning.

## 7. HaikuPorts dependency availability and licenses

Recipes under `/boot/home/Desktop/haikuports`. Version in the recipe filename:

| Dependency | Recipe | Version | License | Core-legal? |
| --- | --- | --- | --- | --- |
| libssh2 | `net-libs/libssh2/libssh2-1.11.1.recipe` | 1.11.1 | BSD-3-Clause | yes (SFTP interop) |
| mbedTLS 2.x | `net-libs/mbedtls/mbedtls-2.28.9.recipe` | 2.28.9 | Apache-2.0 | yes |
| mbedTLS 3.x | `net-libs/mbedtls/mbedtls3-3.6.5.recipe` | 3.6.5 | Apache-2.0 | yes (preferred if bundling) |
| OpenSSL 3.x | `dev-libs/openssl/openssl3-3.5.6.recipe` | 3.5.6 | Apache-2.0 (recipe tag "OpenSSL") | yes |
| OpenSSL 1.1 | `dev-libs/openssl/openssl-1.1.1w.recipe` | 1.1.1w | OpenSSL/SSLeay | yes (permissive) |
| mDNSResponder | `net-dns/mdnsresponder/mdnsresponder-2200.140.11.recipe` | 2200.140.11 | Apache-2.0 / BSD-3 | yes (optional discovery dep) |
| Avahi | `net-dns/avahi/avahi-0.8.recipe` | 0.8 | LGPL-2.1 | **NO - keep out of core** |
| libsmb2 | installed package `libsmb2-4.0.0-1` | 4.0.0 | LGPL-2.1 | **NO - optional/ only** (SMB interop) |

CONFIRMED. This answers open question #3: libssh2, mbedTLS, and OpenSSL are all present;
Apple mDNSResponder (Apache-2.0/BSD) exists as an optional discovery dependency, while
Avahi is LGPL and stays out of core. libsmb2 (SMB2/3, LGPL-2.1) is likewise LGPL, so the SMB
interop backend lives under `optional/` and ships in a separate package (docs/SMB.md).

### libsmb2 synchronous API (verified 2026-07-18 against installed smb2/libsmb2.h)

The read subset the SMB backend uses, all synchronous (each drives its own event loop):
- Context: `struct smb2_context *smb2_init_context(void)`, `void smb2_destroy_context(...)`,
  `void smb2_set_user/password/domain(smb2, const char*)`, `const char *smb2_get_error(smb2)`.
- Connect: `int smb2_connect_share(smb2, server, share, user)` returns `0` or `-errno`;
  `int smb2_disconnect_share(smb2)`.
- Dir: `struct smb2dir *smb2_opendir(smb2, path)` (NULL on error), `struct smb2dirent
  *smb2_readdir(smb2, dir)` (NULL at end), `void smb2_closedir(smb2, dir)`.
- File: `struct smb2fh *smb2_open(smb2, path, flags)`, `int smb2_pread(smb2, fh, uint8_t*,
  uint32_t count, uint64_t offset)` (bytes read or `-errno`), `int smb2_close(smb2, fh)`.
- Stat: `int smb2_stat(smb2, path, struct smb2_stat_64*)` returns `0` or `-errno`.
- `struct smb2_stat_64 { uint32_t smb2_type; uint32_t smb2_nlink; uint64_t smb2_ino, smb2_size;
  uint64_t smb2_atime, smb2_mtime, smb2_ctime, smb2_btime (+ _nsec); }`. `smb2_btime` is a real
  birth time, mapped to the BFS creation time. `SMB2_TYPE_FILE=0x0`, `SMB2_TYPE_DIRECTORY=0x1`,
  `SMB2_TYPE_LINK=0x2`. `struct smb2dirent { const char *name; struct smb2_stat_64 st; }`.
- Paths are share-relative with '/' separators; the share root is `""`.
- Share enumeration (auto-fill the share list so the user only types login): `int
  smb2_share_enum_async(smb2, cb, cb_data)` over an IPC$ connection (async only), driven
  synchronously with `smb2_get_fd` + `smb2_which_events` + `int smb2_service(smb2, revents)` in a
  poll loop. The callback's `command_data` is `struct srvsvc_netshareenumall_rep*` (free with
  `smb2_free_data`, declared in libsmb2-raw.h); shares are `rep->ctr->ctr1.array[i]`
  (`srvsvc_netshareinfo1 {name, type, comment}`), `type & 3 == SHARE_TYPE_DISKTREE` is a file
  share, `type & SHARE_TYPE_HIDDEN` marks C$/ADMIN$. Header include order matters: `smb2/smb2.h`
  must precede `smb2/libsmb2-raw.h`. Live against Windows 192.168.2.104: anonymous IPC$ is refused
  (STATUS_INVALID_PARAMETER, modern Windows blocks it), so enumeration needs the user's login.
CONFIRMED (headers + live connect). A live mount + a live authenticated enumeration are open
items below.

## 8. Storage locations (open question #9)

`headers/os/storage/FindDirectory.h`.

- `status_t find_directory(directory_which, dev_t, bool, char*, int32)` (`:173`) and
  `status_t find_directory(directory_which, BPath*, bool=false, BVolume*=NULL)` (`:207`).
  No `BPathFinder`; the modern alternative is `find_path*` + `path_base_directory`
  (`:135-195`). CONFIRMED.
- `B_USER_SETTINGS_DIRECTORY` (`:66`, ~/config/settings) for node config, the
  trust/pinning store, and shared-folder config. `B_USER_DATA_DIRECTORY` (`:72`),
  `B_USER_CACHE_DIRECTORY` (`:73`), `B_USER_DIRECTORY=3000` (`:60`, home),
  `B_SYSTEM_SETTINGS_DIRECTORY=2010` (`:38`). CONFIRMED.
- Recommendation: config + trust store under `B_USER_SETTINGS_DIRECTORY/Campiello/`; the
  user-visible "Condivisa" folder belongs under home (`B_USER_DIRECTORY`) or a
  user-chosen path, not under settings.
- DECIDED (M-now, `src/traghetto/trust/Paths.cpp`): identity `identity.pem` and trust store
  `trusted` under `B_USER_SETTINGS_DIRECTORY/Campiello/`; the shared root is
  `B_USER_DIRECTORY/Desktop/Condivisa`, created (0755) on first run (off Haiku, `$HOME`
  equivalents). The folder name is user-facing, so it is Italian ("Condivisa"). The daemon's
  default CNP TCP port is 7735 (fixed until Bricola advertises the bound port; overridable via
  `ServerConfig::port`).

## 9. Identity key storage (open question #5)

`headers/os/app/KeyStore.h`, `headers/os/app/Key.h`.

- `BKeyStore` can hold arbitrary binary blobs: `BKey::SetData/GetData/Data/DataLength`
  back a `BMallocIO` (`Key.h:63-66`); use `B_KEY_TYPE_GENERIC` (`Key.h:25-30`) +
  `B_KEY_PURPOSE_GENERIC` (`Key.h:15-22`). Methods `AddKey` (`KeyStore.h:38`), `GetKey`
  (`:17`), `RemoveKey` (`:40`). CONFIRMED.
- CAVEAT: the keystore requires an unlocked keyring and goes through `keystore_server`
  IPC (`KeyStore.h:60,77,98`). On a zero-config unlocked default keyring, secrets are
  readable by any app, which does not by itself satisfy the "always-on, never-visible"
  security goal.
- **Decision**: default is a `0600` file under `B_USER_SETTINGS_DIRECTORY/Campiello/` for
  the private identity key (simpler, no server dependency, keeps the "never shows a key"
  experience). Keystore is an optional/advanced path.

## 10. Deskbar replicant and pairing prompt

- Replicant install: `BDeskbar::AddItem(BView*, int32*=NULL)` and `AddItem(entry_ref*,
  int32*=NULL)` (`headers/os/interface/Deskbar.h:63-65`); also `RemoveItem`, `HasItem`,
  `CountItems`, `GetItemInfo` (`:54-67`). Persistent install (survives reboot) uses the
  `entry_ref` overload pointing at the installed replicant add-on. CONFIRMED.
- Replicant contract: a `BView` with `static BArchivable* Instantiate(BMessage*)` and
  `virtual status_t Archive(BMessage*, bool) const` (`headers/os/interface/Dragger.h:34-35`).
  CONFIRMED.
- Pairing prompt: `BAlert(title, text, button1, button2=NULL, button3=NULL,
  width=B_WIDTH_AS_USUAL, type=B_INFO_ALERT)` with `int32 Go()` (modal) or
  `status_t Go(BInvoker*)` (async) (`headers/os/interface/Alert.h:40-45,69-70`); use
  `B_WARNING_ALERT`. Background prompts via `BNotification(notification_type)` +
  `SetTitle`/`SetContent`/`Send(bigtime_t=-1)` (`headers/os/app/Notification.h:28,46,49,74`).
  Synchronous `Go()` semantics (Haiku API reference, not stated in the header): returns the
  0-based index of the clicked button counting left to right, or -1 if the alert is asked to
  quit, and DELETES the `BAlert` before returning (so it is never freed or touched by the
  caller afterward). CONFIRMED. Implemented in `src/traghetto/server/HaikuPairingPrompt.*`,
  build + fail-safe-when-no-`BApplication` checked on Haiku (`tests/ui/`); `be_app`
  (`headers/os/app/Application.h:169`), `B_ESCAPE` (`headers/os/interface/InterfaceDefs.h:73`).
  CONFIRMED.

## 11. Bricola mDNS multicast socket (verified 2026-07-16 against installed headers)

Discovery (Bricola) joins the link-local mDNS group and speaks DNS-SD for
`_campiello._tcp`. The BSD socket surface it needs is present in the installed Haiku POSIX
headers with the standard names and values:

- Port reuse: `SO_REUSEADDR` `0x40`, `SO_REUSEPORT` `0x80` (`posix/sys/socket.h:63-64`).
  Both are set before `bind()` so several mDNS listeners (other apps, and our own tests'
  two sockets) can share UDP 5353. CONFIRMED.
- Multicast: `IP_MULTICAST_IF` 9, `IP_MULTICAST_TTL` 10, `IP_MULTICAST_LOOP` 11,
  `IP_ADD_MEMBERSHIP` 12, `IP_DROP_MEMBERSHIP` 13, all at level `IPPROTO_IP` 0
  (`posix/netinet/in.h:131-137,54`). `struct ip_mreq { struct in_addr imr_multiaddr;
  struct in_addr imr_interface; }` (`posix/netinet/in.h:97-100`), the standard 2-field
  layout. `INADDR_ANY` `0x00000000` (`:170`). CONFIRMED.
- The mDNS group is `224.0.0.251` on port `5353` (RFC 6762 section 3). TTL is set to 1 to
  stay on the link; loopback is enabled so same-host peers hear each other.
- On Haiku the socket calls (`socket`/`bind`/`setsockopt`/`sendto`/`recvfrom`/`inet_pton`/
  `inet_ntop`) link from **`libnetwork`** (`-lnetwork`), not libc; on Linux they are in
  libc. CONFIRMED at build (`tests/bricola/`).

Implemented in `src/bricola/mdns/`: the multicast socket (`MdnsSocket.*`), the wire codec
(`MdnsWire.*`: name/record/TXT encode+decode, query/response build), the browse/reconcile
layer (`PeerTable.*` folds PTR/SRV/TXT/A records into whole `Peer` entries with TTL expiry and
goodbye handling; `Browser.*` is the seam between datagrams and the table), the advertise
side (`Responder.*` builds our node's PTR/SRV/TXT/A announce from a `ServiceInfo`, answers
service PTR/ANY queries, and emits a TTL-0 goodbye; pragmatic M2 scope, no probing/conflict
resolution), and the facade (`Bricola.*` owns the socket, Responder, and Browser and drives
them from one worker thread: announce on start and on a 30 s timer, answer queries, ingest
responses, expire stale peers, goodbye on Stop; PeerObserver callbacks fire on the worker
thread, Peers() is a mutex-guarded snapshot). All are pure-std and unit-tested off Haiku
(`tests/bricola/`) by feeding packets built with `MdnsWire` (the responder's announce is
round-tripped through the browser). The resident daemon wires it in
(`src/traghetto/server/DaemonApp.cpp`): after `ServerNode::Start`, it builds a `ServiceInfo`
from the bound port, node name, and identity fingerprint (`wire::kProtocolVersion`,
`ToHex(IdentityFingerprint())`) and calls `Bricola::Start`; the whole daemon (with `-lbe`)
compiles and links on Haiku (`tests/server/` daemon-build).

The Deskbar replicant (`src/bricola/replicant/PeerReplicant.cpp`, Haiku-only) is the visible
presence UI: it owns a **browse-only** Bricola (`Bricola::StartBrowsing` skips the responder,
so it does not double-advertise this node; the daemon is the advertiser) and registers a
`PeerObserver` that forwards worker-thread Found/Updated/Lost events to the view via a
`BMessenger` (the "worker never touches UI" idiom). The view mutates its peer list only in
`MessageReceived`. Structure lifted from LocalSend's replicant; verified against the Deskbar
headers (section 10). Build-verified as a shared add-on with `-lbe` (`tests/replicant/`); not
loaded here (a replicant is exercised in a throwaway VM per the working agreement).

**Multicast delivery is runtime-verified (2026-07-16) on real Haiku, same-host, no second
machine.** Two findings, both checked against the network stack here:
- **`INADDR_ANY` does not deliver**: there is no default route for `224.0.0.0/4` in the Haiku
  routing table, so joining/sending with `imr_interface = INADDR_ANY` sends nothing that
  returns. The socket must bind multicast to a concrete interface via `IP_MULTICAST_IF` and
  `imr_interface` (`MdnsSocket::Open(interfaceIpv4)`).
- **`IFF_MULTICAST` is not a usable capability flag on Haiku**: enumerating interfaces with
  `SIOCGIFCONF` + `SIOCGIFFLAGS` (verified `posix/net/if.h`, `posix/sys/sockio.h`;
  `SIOCGIFCONF=8911`, `SIOCGIFFLAGS=8907`, `IF_NAMESIZE=32`, entries advanced by
  `IF_NAMESIZE + sa_len`) shows `IFF_MULTICAST` (`0x8000`) CLEAR on every interface, including
  the loopback that demonstrably carries multicast. So interface selection filters on
  `IFF_UP` and loopback-ness only, never `IFF_MULTICAST`.
  With the interface bound to `127.0.0.1`, a full round-trip works: `MdnsSocket` send->receive
  (`tests/bricola/test_mdnssocket`), and two `Bricola` nodes discover each other and see each
  other's goodbye (`tests/bricola/test_bricola` two-node, and the `discover_demo` tool). The
  daemon auto-selects the primary non-loopback interface (`PrimaryMulticastIPv4`), overridable
  with the `CAMPIELLO_MDNS_IFACE` env var. Real cross-machine delivery over a normal LAN is
  still the one untested case; see the open item below.

---

## Reference projects (verified 2026-07-03, detail in docs/REUSE.md)

- OpenSSL 3 builds, links, and does TLS 1.3 on Haiku - LocalSend links `-lssl -lcrypto`
  and generates self-signed RSA-2048 certs (`LocalSend/src/net/TlsContext.cpp`). Ran only
  with `SSL_VERIFY_NONE`, so mutual-auth + pinning is un-exercised there.
- A dependency-free mDNS/DNS-SD **querier** exists to lift
  (`LANterna/src/enrich/MdnsEnricher.cpp`): raw UDP to 224.0.0.251:5353, full DNS
  name/A/PTR/SRV codec. No responder side, no TXT parsing - the responder is greenfield.
- A complete, correct Deskbar replicant exists to lift
  (`LocalSend/src/replicant/DeskbarReplicant.cpp` + `src/app/DeskbarItem.cpp`).
- Crash-safe settings persistence pattern to lift: atomic temp+rename of a flattened
  `BMessage` under `find_directory(B_USER_SETTINGS_DIRECTORY)/<app>/`
  (`Sotoportego/src/server/ProfileStore.cpp:126-201`).
- No existing project implements persistent identity keys or peer authentication.
  LocalSend regenerates its keypair per launch; Sotoportego delegates all TLS to a
  spawned `openvpn` (GPLv2) subprocess. The TOFU trust core is greenfield.
- `Dogana` has no LICENSE file. Same author; add MIT before lifting code verbatim.

---

## Risks and decisions

1. **FUSE attribute path is read-only and type-lossy. DECIDED: option C (hybrid).**
   The stock userlandfs FUSE bridge maps only `listxattr`/`getxattr`, rejects attribute
   writes with `B_UNSUPPORTED` (`FUSEVolume.cpp:2495-2497`), and collapses all non-MIME
   attribute types to `B_RAW_TYPE` (`:355-359`). Campiello's headline feature is full BFS
   attribute fidelity end to end (M3), which the FUSE path cannot deliver. Decision (see
   the native-interface subsection under section 1, verdict CONFIRMED):
   - **Interop mode uses the FUSE bridge** (SFTP/WebDAV/NFS4 are read-mostly and have no
     Haiku typed attributes to preserve, so the read-only/untyped limitation does not bite).
   - **Native mode (Haiku-to-Haiku) uses the native userlandfs interface**, which exposes
     typed `CreateAttr(..., uint32 type, ...)` + `WriteAttr` (`server/Volume.h:138,148`) so
     BFS attribute type codes round-trip. Preferred implementation: a standard
     `file_system_module_info` against the public, shipped `os/drivers/fs_interface.h`,
     loaded via the installed `libuserlandfs_haiku_kernel.so` - avoids depending on the
     unshipped private C++ base-class headers.
   Reflected in PROPOSAL.md section 8. **Empirically confirmed at runtime (2026-07-04)** by
   a FUSE passthrough of a directory whose files carry typed BFS attributes
   (`src/fondamenta/smoke/campiello_passthrough.c`): through the FUSE bridge, attribute
   VALUES read back but a Text and an Int-32 attribute both surfaced as `raw_data` (only the
   MIME type kept its type), and `listattr` showed zero attributes because the bridge never
   invokes the high-level `listxattr` op (it does call `getxattr` for named reads). So the
   FUSE attribute path is not just read-only and type-lossy but also does not enumerate
   here. **The native front end, by contrast, was runtime-tested (2026-07-04,
   `src/fondamenta/native/`) and preserved the types: `listattr -l` on the native-mounted
   file showed `Text` and `Int-32` (not `raw_data`), and enumerated both attributes.** This
   is the concrete proof of why native mode uses the native `fs_interface` front end and not
   the FUSE bridge.

2. **Type loss on the wire is avoidable in native mode.** CNP carries the real
   `type_code` explicitly (see PROTOCOL.md), so native-to-native fidelity does not depend
   on the FUSE channel. The loss only bites if attributes are routed through the stock
   FUSE xattr shim.

3. **userlandfs mount path verified (2026-07-04); native front end and attributes still to
   test.** The `userland_fs` package is installed and a minimal FUSE add-on builds, mounts
   as a distinct `userlandfs` volume, and unmounts cleanly (see `src/fondamenta/smoke/` and
   section 1). Still runtime-untested: (a) the native `fs_interface` front end that native
   mode uses (decision C), and (b) the attribute round-trip, which the empty-volume smoke
   test does not exercise and which needs a non-empty filesystem. `userland_fs` must remain
   a declared runtime dependency of the package.

4. **Keystore does not meet the invisible-security bar alone.** Default to a `0600`
   identity-key file under find_directory; treat BKeyStore as optional.

5. **Live query delivers node refs, not content.** Distributed live query (M4) must
   resolve each `B_QUERY_UPDATE` node ref locally and read its attributes before streaming
   to peers; the kernel event carries no path and no attribute values.

---

## Open items still needing a live run (not answerable from source alone)

- The native `fs_interface` front end typed attribute round-trip is runtime-verified in
  BOTH directions (`src/fondamenta/native/`): read (enumeration + typed read) and write
  (`create_attr`/`write_attr`/`remove_attr`). Confirmed 2026-07-04 - `addattr -t int32` and
  `-t string` created attributes that `listattr -l` showed as `Int-32`/`Text` and `catattr`
  read back typed. BUT unmount is NOT clean: it panicked the kernel twice (see section 1
  HAZARD). Open, needing a throwaway VM: fix the vnode-lifecycle/unmount issue; then real
  remote-backed content and content (non-attribute) writes.
- The mDNS multicast round-trip is now runtime-verified same-host on real Haiku (see section
  11): binding to a concrete interface (`127.0.0.1` for local, the primary interface for LAN)
  makes send->receive and two-node discovery work; `INADDR_ANY` does not (no default multicast
  route). What remains untested is **real cross-machine delivery over a normal LAN**. It could
  not be exercised here because the only network available is a WiFi SSID ("VePro_Wifi_IoT_01")
  that appears to enforce client isolation: six seconds of listening on 5353 saw zero ambient
  mDNS and the host's own multicast did not return on that interface. On a non-isolated LAN the
  same interface-bound path is expected to work; this needs a second machine on an ordinary
  network to confirm.
- **Same-host cross-process multicast does NOT deliver (verified 2026-07-18).** Two Bricola
  instances in ONE process discover each other (test_bricola), but a browser in a SEPARATE
  process receives nothing from an advertiser process on the same host over loopback: a probe
  browsing on 127.0.0.1 saw zero of a concurrent advertiser's announcements, while that
  advertiser's own in-process browser saw them. With SO_REUSEPORT, the looped-back multicast
  reaches only the sending process's sockets, not another process's. Consequence: native
  discovery cannot be demonstrated on a single machine (the daemon that advertises and the
  discovery add-on that browses are separate processes); it needs two machines on a LAN. This
  does not affect the two-machine product path (each host has one socket; the announcement
  crosses the network). Open, needing two machines: whether a network (not looped) multicast is
  delivered to MULTIPLE local REUSEPORT listeners (relevant if a host runs both the daemon and
  the discovery add-on and both must hear remote peers).
- **SMB mount on Haiku: RESOLVED end to end (2026-07-19).** A real Windows share mounts as a
  userlandfs volume and its files list/open/read correctly (verified against 192.168.2.104, share
  `din esp8266 mini`: `df` shows it, `ls` lists 8 files, a copied file is byte-identical). Three
  nested bugs, see docs/SMB.md for the full account:
  1. **libsmb2 EINPROGRESS.** `smb2_connect_share` failed with `Operation now in
     progress(-2147454940)`. libsmb2's `errno != EINPROGRESS` needs its EINPROGRESS constant to
     match the runtime errno, but haikuporter injects `-DB_USE_POSITIVE_POSIX_ERRORS` into
     CMAKE_C_FLAGS by default (positive constant `+2147454940`) while the runtime errno is native
     `-2147454940` in a normal app; and the two host contexts (app vs `userlandfs_server`) can
     disagree on sign. Baking the mapper into libsmb2.so fixed connect but broke the async wait.
     Fix (recipe REVISION 6, vendored `packaging/smb/libsmb2/`): override `CMAKE_C_FLAGS=""` (native
     constants) AND make the three would-block checks in `lib/socket.c` sign-agnostic
     (`errno != C && errno != -C`). An in-child probe confirmed the mount child sees native errno,
     which now matches. No mapper, so the lib also loads cleanly as an add-on dependency.
  2. **getattr("/").** The mount's first call `getattr("/")` -> `Stat("")` -> `smb2_stat("")` on the
     share root, which some servers reject. Fix: `SmbBackend::Stat` returns a synthetic directory
     for the share root without a network stat.
  3. **share names with spaces.** userlandfs whitespace-splits the mount parameter string, so
     `share=din esp8266 mini` truncated to `din` (STATUS_BAD_NETWORK_NAME). Fix: percent-encode
     values in `BuildSmbMountParameters` (`EncodeMountValue`), decode in `ParseSmbMount`.
  Gotcha that cost hours: `userlandfs_server <addon>` stays resident and serves every mount of that
  add-on from its first loaded image; after reinstalling the add-on or libsmb2 you must kill the
  stale `userlandfs_server campiello_smb` process or the next mount reuses the old binary. Killing
  it can also leave userlandfs in a "Bad port ID" state where mounts then fail until a reboot;
  prefer rebooting to pick up a new build.
- **SMB server-level browse (Windows `\\server` style): live-verified (2026-07-19).** A no-share
  mount (`SmbServerBackend`) of 192.168.2.104 mounted at `/192.168.2.104`; its root listed the
  share `din esp8266 mini` as a folder, and navigating into it listed the real files. This is the
  WON double-click flow: log in once, then browse all shares as folders. Verified after a reboot
  (needed to clear the userlandfs state churned by build iteration).
- Whether the older in-tree `nfs`/`nfs4` clients are good enough to expose directly, or
  should be wrapped (open question #6) - needs a functional test.
- The negotiated drag-and-drop `BMessage` format (`B_SIMPLE_DATA`, `be:` field names) for
  Bossolo (open question #7) - deferred to before M6.
- MUSCLE's exact license, only if its code is ever reused (open question #8); the
  preferred path builds native on `BMessage`.

## M4 distributed query: BQuery / BVolume / NodeMonitor (verified 2026-07-21)

Checked against the installed Haiku headers before use (working agreement rule 1):

- **BQuery** (`headers/os/storage/Query.h`): `SetVolume(const BVolume*)` (:63),
  `SetPredicate(const char*)` (:64), `SetTarget(BMessenger)` (:65, the ONLY SetTarget overload,
  no `BHandler*` form), `SetFlags(uint32)` (:66), `Fetch()` (:76), `GetNextRef(entry_ref*)` (:80),
  `GetNextEntry(BEntry*, bool)` (:79), `IsLive()` (:68). A non-live query (no SetTarget) is a plain
  fetch; SetTarget makes it live.
- **BVolume** (`headers/os/storage/Volume.h`): `BVolume(dev_t)` (:24), `SetTo(dev_t)` (:29),
  `KnowsQuery() const` (:53). The shared root's volume is obtained from
  `BEntry(root).GetNodeRef(&nref)` then `BVolume(nref.device)`; a volume where `KnowsQuery()` is
  false yields a `kUnsupported` reply.
- **NodeMonitor** (`headers/os/storage/NodeMonitor.h`): `B_ENTRY_CREATED = 1` (:36),
  `B_ENTRY_REMOVED = 2` (:37). These are the live `QUERY_UPDATE` opcodes; `added=true` maps to
  created, `added=false` to removed.

Implementation status:
- Wire (`QUERY_OPEN/RESULT/UPDATE/CLOSE`), server (`FileServer::HandleQueryOpen` via BQuery, with
  the shared-root boundary filter + a result quota), client (`CnpBackend::Query`), and the
  aggregator + virtual `/.query/<predicate>` folder (`QueryAggregator` + `PathRouter`) are all
  implemented and tested. The initial-query path is verified end to end
  (`tests/fondamenta/test_cnpbackend.cpp`: a marker file found by a name query through the real
  server + BQuery on the loopback).
- The live loop is done and verified end to end. ServeConnection was split into a sole reader thread
  (SSL_read) and a dedicated `FrameWriter` thread (SSL_write) per connection, the concurrency a
  TLS 1.3 connection supports (no renegotiation). The server opens a live BQuery (B_LIVE_QUERY,
  `SetTarget(BMessenger)`) with a per-query `BHandler` on a `BLooper`; each `B_QUERY_UPDATE` is
  resolved within the shared root and pushed as `QUERY_UPDATE` through the writer. The client
  `LiveQueryClient` keeps the query open on a dedicated channel and delivers each update to a
  callback. `tests/fondamenta/test_cnpbackend.cpp` verifies the full path over a real connection:
  create a file on the server -> receive the added update; remove it -> receive the removed update
  (64 checks). `tests/dispatch/test_framewriter.cpp` covers the writer's ordering/overflow/failure.
- Remaining follow-up: batched `QUERY_RESULT` (`done=false`) for very large initial sets, a minor
  optimization only.
