# campiello_nfs - NFS add-on

A Campiello device add-on (see `docs/DEVICE_ADDONS.md`) for an NFS server. It lists the server's
exports and shows how to mount one on Haiku. It does **not** mount anything itself, and it does not
implement an NFS filesystem client. Ninth component of the suite in `docs/ADDONS_SUITE.md`.

## What it is

NFS (Network File System) servers export directories to the LAN, advertised over mDNS as `_nfs._tcp`.
Users want to see what is exported and mount it.

## How it works (protocol)

- Listing exports is what `showmount -e` does, over **ONC RPC** (RFC 1057) on UDP:
  1. The **portmapper** (program `100000` v2, UDP port 111), `PMAPPROC_GETPORT`, returns the UDP port
     of the mount daemon (`MOUNT` program `100005` v3).
  2. `MOUNTPROC3_EXPORT` on that port returns the export list: a linked list of `{ dirpath, groups }`.
  RPC messages are XDR: 4-byte big-endian integers and length-prefixed, 4-byte-padded strings; the
  call carries `AUTH_NULL` credentials.
- **Mounting** is a `mount -t nfs4` operation (or Tracker's Disks > Mount). The add-on shows the
  mount address (`host:/export`) and the command, and copies it to the clipboard; it never mounts,
  because mounting/unmounting a userlandfs volume is a KDL hazard (CONTRIBUTING.md).
- A **native NFS client** (the actual file protocol: RFC 1813 NFSv3 or RFC 7530 NFSv4, file handles,
  READ/WRITE/READDIR) is a large module and a documented follow-up.

## Integration into Campiello

- `optional/nfs/NfsProbe.{h,cpp}`: hand-rolled ONC RPC over UDP. `MountPort` (portmap GETPORT) and
  `ListExports` (MOUNT EXPORT), with XDR helpers (`rpc::PutU32`/`PutString`/`GetU32`/`GetString`,
  `BuildCall`, `ParseReplyHeader`, `ParseExportList`). No third-party library, so **MIT-clean**. The
  RPC codec is unit-tested.
- `optional/nfs/campiello_nfs.cpp`: the app. Probes the exports on a worker thread and lists them
  (`host:/export`), with mount instructions and a "Copia indirizzo" button. `RefsReceived` reads
  `CAMPIELLO:host/name` from the WON device shortcut. It performs no mount.
- `optional/nfs/nfs.handler`: matches `_nfs._tcp`. `packaging/nfs` builds `campiello_nfs-0.1.0-1`
  (needs only `libbe`/`libnetwork`).

## Licensing

No third-party code or library. RPC/MOUNT are implemented from the RFCs. Fits the MIT core rule (kept
under `optional/` only because it is device-specific).

## Reference material

- RFC 1057 (ONC RPC) and RFC 1833 (Binding Protocols / portmapper) for the RPC/portmap wire format.
- RFC 1813 (NFSv3) for the MOUNT v3 protocol and `MOUNTPROC3_EXPORT`.
- `showmount(8)` (from nfs-utils) as the behavioral reference.

## Testing status

The RPC codec is unit-tested (a synthetic `MOUNTPROC3_EXPORT` reply decodes to the right export paths;
the GETPORT call encodes to the expected 56 bytes). **Not yet validated against a live NFS server**
(none in the dev environment). To validate: run `campiello_nfs host=<server-ip>` and check the export
list.

## Follow-ups

- A native NFSv3/NFSv4 client (READ/READDIR over RPC) to browse and download without mounting.
- Verify Haiku's exact `mount -t nfs4` parameter syntax across versions and tailor the instructions.
- Offer a one-click mount that opens Tracker's mount UI (still user-driven, never automatic).
