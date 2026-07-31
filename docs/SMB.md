# SMB interop design note (Windows shared drives)

Status: **approved, in progress.** Browse Windows SMB/CIFS shares in Campiello, the interop
counterpart to the SFTP path (docs/M1.md). Read together with M1.md (the PeerBackend + FUSE
front end + connect helper it mirrors).

## Goal
Mount a Windows shared folder (the default File-and-Printer-Sharing SMB share, `\\PC\Share`) as
a read-only volume you browse in Tracker, without installing anything on the Windows side (SMB
is on by default, unlike the OpenSSH the SFTP path needs).

## Licensing: why this lives under optional/ (the hard part)
There is no MIT/BSD SMB client. The realistic clients are:
- **libsmb2** (github.com/sahlberg/libsmb2): SMB2/3, works with modern Windows, lightweight.
  License **LGPL-2.1** (confirmed from the installed package attributes).
- libsmbclient (Samba): SMB1/2/3, **GPL v3** (confirmed from the HaikuPorts recipe), heavy.
- libdsm: SMB1 only, so it cannot talk to a default modern Windows (SMB1 disabled since Win10).

We use **libsmb2 (LGPL-2.1)**. Per the working agreement (CONTRIBUTING.md), LGPL/GPL code lives
only under `optional/`, dynamically loaded, off by default. LGPL's dynamic-linking exception
means our own code stays MIT while linking `libsmb2.so`. So:
- The SMB backend lives under **`optional/smb/`** (the existing placeholder).
- It ships as a **separate optional package** (`campiello_smb`) that `requires lib:libsmb2`; the
  core `campiello` package stays MIT and libsmb2-free. Users install the SMB add-on only if they
  want Windows shares.

## Architecture (parallel to SFTP, reusing everything)
```
Tracker -> userlandfs (FUSE bridge) -> Fondamenta FUSE front end (CampielloFuse, DONE)
        -> PeerBackend (DONE) -> SmbBackend (libsmb2)  <- NEW, optional/smb/
        -> a Windows SMB share
```
`SmbBackend : PeerBackend` implements the read subset (Stat/ReadDir/Open/Read/Close) with
libsmb2's synchronous API (`smb2_connect_share`, `smb2_opendir`/`readdir`/`closedir`,
`smb2_open`/`pread`/`close`, `smb2_stat`). The FUSE front end and a connect helper are reused
from M1; only the backend differs.

Differences from SFTP:
- **No host-key TOFU.** SMB authenticates with user / password / domain; there is no SSH-style
  host key to pin, so there is no SmbKnownHosts. Trust is the credentials and the share.
- **SMB2/3** negotiated by libsmb2 (works with default Windows sharing).
- Config: server, share, user, password, domain, and an optional base path within the share.
- Type/stat mapping is clean: `smb2_stat_64` carries type (file/dir/link), size, mtime, and even
  a birth time (`smb2_btime`) that maps to the BFS creation time.

## Commit plan
1. **`SmbBackend`** (optional/smb/): libsmb2 session + the PeerBackend read ops; an integration
   test driven by `CAMPIELLO_SMB_*` env vars against a real share (SKIPs without one). *(this
   commit)*
2. **SMB add-on** (`campiello_smb_main`): parses the mount target, connects an SmbBackend, and
   runs CampielloFuseMain. Build-verified.
3. **Connect helper** (or extend the existing one) to mount a Windows share by GUI.
4. **Optional package** `campiello_smb` (requires lib:libsmb2), separate from the core package.

## RESOLVED end to end: SMB shares mount and browse (2026-07-19)
A real Windows share (a Microsoft-account login) now mounts as a userlandfs volume and its files
list, open and read correctly. Getting there meant fixing three nested bugs plus one userlandfs
gotcha. Verified against `192.168.2.104`, share `din esp8266 mini`: `df` shows the volume, `ls`
lists the files, and copying a file off it yields byte-identical content.

### Bug 1: libsmb2's connect rejected its own EINPROGRESS
`smb2_connect_share` failed with `Connect failed with errno : Operation now in
progress(-2147454940)`. libsmb2's socket code does `if (connect(...) != 0 && errno != EINPROGRESS)`.
For that to work, libsmb2's `EINPROGRESS` constant must match the RUNTIME errno. The magnitudes:
native `EINPROGRESS == -2147454940`, positive (`B_USE_POSITIVE_POSIX_ERRORS`) `== +2147454940`.

The trap: **haikuporter injects `-DB_USE_POSITIVE_POSIX_ERRORS` into `CMAKE_C_FLAGS` by default**
(the recipe never set it), so libsmb2 got the POSITIVE constant, while the runtime errno is native
negative in a normal process. Worse, the two host contexts disagree: a normal app sees native
errno, but `userlandfs_server` may see positive. No single-sign build works everywhere, and baking
`libposix_error_mapper` into `libsmb2.so` (REVISION 2/5) fixed the initial connect but broke the
async wait (timeout).

**Fix (recipe REVISION 6, vendored at `packaging/smb/libsmb2/`):** override `CMAKE_C_FLAGS=""` so
the errno constants are native, AND make libsmb2's three would-block checks (`EINPROGRESS`,
`EAGAIN`/`EWOULDBLOCK`, `EINTR`/`EAGAIN` in `lib/socket.c`) SIGN-AGNOSTIC (`errno != C && errno !=
-C`). Then the connect + NTLM auth succeed in a normal app AND inside `userlandfs_server`. No
mapper, so the library also loads cleanly as an add-on dependency. Verified with an in-child probe:
the mount child sees native errno (`-2147454940`), which now matches. The recipe change should go
upstream to HaikuPorts. `SmbBackend` reads libsmb2's `-errno` returns with the native convention
(test `rc != 0`, map `-rc` with native `ENOENT`/`EACCES`), built WITHOUT the positive flag.

### Bug 2: the mount's first getattr("/") hit the network
userlandfs mounts by first calling `getattr("/")`, which mapped to `SmbBackend::Stat("")` ->
`smb2_stat("")` on the share root, which some servers reject, aborting the whole mount. **Fix:**
`SmbBackend::Stat` presents the share root as a synthetic directory (mode `S_IFDIR | 0555`) with no
network stat. A share root is always a directory.

### Bug 3: share names with spaces were truncated
The real share is named `din esp8266 mini`. userlandfs whitespace-tokenizes the mount parameter
string before the add-on parses it, so `share=din esp8266 mini` became `share=din` (plus stray
tokens) -> `STATUS_BAD_NETWORK_NAME`. **Fix:** `BuildSmbMountParameters` percent-encodes values
(`EncodeMountValue`, `%20` for space, `%25` for `%`) and `ParseSmbMount` decodes them
(`PercentDecode`). The password is encoded the same way, so it may now contain spaces.

### Gotcha: userlandfs caches a per-add-on server
`userlandfs_server <addon>` stays resident and serves every mount of that add-on from its FIRST
loaded image. After reinstalling the add-on or libsmb2 you MUST kill the stale
`userlandfs_server campiello_smb` process or the next mount silently reuses the old binary. This
wasted hours of debugging (mounts kept using superseded builds).

## Two mount modes: whole server ("\\server") vs a single share
There are two ways to mount, chosen by whether the mount parameters carry a `share=`:
- **Server-level (default in the helper), Windows `\\server` style.** No `share=`: the add-on uses
  `SmbServerBackend`, whose volume root lists the server's shares as folders; navigating into a
  share connects to it lazily (a cached `SmbBackend` per share) and browses its files. Paths route
  as `/<share>/<path within share>`; open handles are remapped to one flat namespace. This is what
  a double-click on a host in the WON folder gives: enter user + password once, then browse all
  shares like folders. `SplitServerPath` (unit-tested in `test_smbserver`) is the path router.
- **Share-level.** A `share=<name>` mounts that one share directly (`SmbBackend`), volume root =
  the share root. The helper's "Elenca condivisioni" button fills the share field for this.

The helper validates the login before mounting: server-level via `EnumShares` (which also confirms
the credentials over IPC$), share-level via `Connect`. Values in the parameter string are
percent-encoded, so a share/password with spaces (e.g. `din esp8266 mini`) works. In the dialog the
share is a first-class field (empty = whole server); the "Elenca condivisioni" button fills it with
a picked share. Login (user, domain) is remembered in `<settings>/Campiello/smb_recent`.

### Remembering the password (opt-in, encrypted)
Haiku's keystore (BKeyStore) is the right home for a password, but its developer headers are not
present in this SDK, so instead the helper encrypts the password itself. An opt-in "Ricorda
password" checkbox (default off) stores it with **AES-256-GCM** (OpenSSL/libcrypto, Apache-2.0)
under a per-installation 32-byte key kept in an owner-only (0600) `<settings>/Campiello/smb_secret.key`;
the ciphertext (nonce | ct | tag, hex-encoded) goes in the owner-only `<settings>/Campiello/smb_secrets`,
keyed by server+user, with the server+user bound in as GCM AAD. The secrets file is useless without
the keyfile (e.g. if copied to another machine), and a wrong key or tampering fails the tag check.
Caveat: a process running as this user can read both files, so this protects at rest, not against
local code running as you (same limitation as most keystores). Unchecking the box removes the
entry. Only the helper links libcrypto; the add-on never touches the store (it gets the password
through the mount parameters). If the keystore headers appear later, move to BKeyStore.

### Making reconnection effortless
- **Per-host memory.** A successful connect records this server's choices (user, domain, share,
  "as disk", disk name) in `<settings>/Campiello/smb_hosts` (0600, string fields hex-encoded), so
  opening the dialog for a known host prefills everything. With a remembered password the dialog
  even connects on its own (one tap).
- **Auto-mount at login.** The "Monta i miei share all'avvio" checkbox installs a small script at
  `~/config/settings/boot/launch/campiello-smb-automount` that runs `campiello_smb_mount
  --automount` at login; that headless mode walks `smb_hosts` and mounts every host whose password
  is stored. Ticking it implies "remember password" (auto-mount needs it). Unchecking removes the
  script. No password, no unattended mount.
- The connect/mount and the share enumeration run on worker threads so the dialog never freezes.
- **Unmount ("Smonta").** The dialog has a "Smonta" button that unmounts this host's volume
  (`fs_unmount_volume`) after an explicit confirmation whose text warns about the KDL hazard
  (unmounting a userlandfs volume has kernel-panicked on Haiku - working agreement). The safe
  default in the confirmation is "Annulla". Until unmount is verified safe in a VM, prefer rebooting
  to clear mounts; the button is there for when you accept the risk.

## Writing to a share (read-write mount)
The volume can be mounted read-write, not just read-only. The write path spans every layer:
- **SmbBackend** implements the PeerBackend write subset over libsmb2: `OpenWrite`
  (`smb2_open O_WRONLY|O_CREAT`, no `O_TRUNC` so opening an existing file never loses it),
  `Write` (`smb2_pwrite`), `Mkdir`, `Unlink` (dispatches `smb2_rmdir` for a directory,
  `smb2_unlink` for a file), `Rename`, `Truncate`. Paths with a `..` component are rejected
  (`kInvalidRequest`) so a request cannot escape the share.
- **CampielloFuse** wires the FUSE write ops (`create`, `write`, `mkdir`, `unlink`, `rmdir`,
  `rename`, `truncate`, `ftruncate`) and lets `open` take a write intent. This is shared, so SFTP
  gets it too; a backend that leaves the write methods at their `kUnsupported` default returns
  `-ENOSYS`, so wiring them is safe for all. On a read-only mount the kernel never calls them.
- **SmbServerBackend** routes each write to the per-share backend; the server root and a bare share
  are directories you cannot write, and a rename must stay within one share (SMB limitation).
- **The helper** mounts read-write by default; a "Sola lettura" checkbox (remembered per host)
  mounts `B_MOUNT_READ_ONLY` instead.

Not yet: **BFS attributes**. SMB has no BFS-style attributes, so attribute writes are not wired
(Tracker can create/copy/rename/delete files, but not set file types/icons/positions on the share).
Accept-and-ignore or NTFS ADS mapping is a later step. Write support is new: test it in a VM on
non-critical data first (the KDL unmount hazard also applies).

### Notes
- The `campiello_smb` package requires libsmb2 REVISION 6+ (native errno, sign-agnostic socket.c).
- SMB signing: libsmb2 negotiates it; the real share connected with
  `SMB2_NEGOTIATE_SIGNING_ENABLED` and no extra configuration.
- KDL hazard: mounting is fine, but unmounting a userlandfs volume has kernel-panicked before, so
  leave test mounts in place and reboot rather than unmounting on shared hardware. Related: the
  resident `userlandfs_server <addon>` caches the first-loaded add-on image, so after reinstalling
  you must kill it for the new build to load; but killing it can leave userlandfs in a bad state
  ("Bad port ID" / mounts then fail) until a reboot. Prefer rebooting to pick up a new build.
