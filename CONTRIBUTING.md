# Contributing to Campiello

Conventions for working on Campiello.

## Code and dependencies

- Verify every Haiku / FUSE / BFS API against the Haiku source headers before use. Never invent a
  signature; if an API cannot be located, stop rather than guess.
- Core dependencies must be MIT, BSD, Apache-2.0, ISC, zlib, or public domain. LGPL/GPL code lives
  only under `optional/`, dynamically loaded and off by default. If something in core seems to need
  an LGPL/GPL library, raise it first.
- Target the libfuse 2.x high-level API (`struct fuse_operations`), not FUSE 3.x.
- English in code and developer docs; Italian only for end-user strings.
- Prefer small, reviewable commits. Propose an interface and a short design note before large,
  non-trivial components.
- Keep `docs/PROTOCOL.md` and `docs/VERIFIED.md` authoritative: update them in the same commit that
  changes the wire format or a verified fact.

## Product principles

- Experience first: one `hpkg` install, zero-config first use, and security that is always on but
  never shows a key, certificate, or CA. Only ever a one-tap "allow this computer" prompt. No
  terminal step or config file on the default path; if a feature needs one, put it behind an
  advanced toggle.
- Treat every peer as untrusted: validate paths and attributes on both ends. No path escape, no
  unchecked attribute writes.

## Safety

- Never mount or unmount a userlandfs filesystem on a machine you cannot afford to reboot.
  Unmounting a userlandfs volume has kernel-panicked (`vfs.cpp: vnodes.IsEmpty()`, KDL); `unmount`
  can return success while teardown is incomplete. Test mounted filesystems only in a throwaway VM.
  Building and compiling the fs modules is safe; loading and mounting them is not, on shared
  hardware.

## Orientation

- `docs/PROPOSAL.md` is the driving design document.
- `docs/PROTOCOL.md` is the CNP wire specification.
- `docs/VERIFIED.md` records facts checked against source, with citations.
- `docs/REUSE.md` maps what was harvested from sibling projects and what is greenfield.
