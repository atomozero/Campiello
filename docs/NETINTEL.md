# NetIntel: passive LAN intelligence

`src/vicinato/NetIntel.{h,cpp}` gathers four small, dependency-free LAN capabilities that enrich or
extend what mDNS + the SMB sweep already surface in the WON app. Each is a port of the corresponding
piece of the sibling LANterna project (MIT, atomozero), reworked into one module under
`campiello::vicinato`. Portable POSIX sockets only (the surface `SmbHostFinder`/`TcpPing` already use
on Haiku); the pure encode/parse helpers are unit-tested off Haiku in `tests/bricola/test_netintel.cpp`.

## What it provides

| Function | What it does | Wire detail |
|----------|--------------|-------------|
| `ReadArpCache()` | IP -> MAC from the kernel ARP table | Haiku: parses `arp -a`; Linux: `/proc/net/arp` |
| `OuiDatabase` | MAC prefix -> manufacturer | reads an IEEE `oui.txt`; the DATA is reused, never Wireshark's GPL `manuf` |
| `SendWakeOnLan()` | wakes a host | 102-byte magic packet (6x 0xFF + 16x MAC), UDP broadcast, port 9 |
| `QueryNetBiosName()` | Windows computer name of an SMB host | UDP 137 NBSTAT (RFC 1001/1002) |
| `ResolveReverseDns()` | hostname fallback when 137 is closed | reverse-DNS PTR via `getnameinfo(NI_NAMEREQD)` |
| `DiscoverSsdp()` | finds TVs, Sonos, NAS, routers mDNS misses | SSDP M-SEARCH to 239.255.255.250:1900 |

## How the WON app uses it

A background `IntelThread` runs a periodic (45 s) enrichment pass off the window thread and folds
the results back into the service list and the details pane:

- **Produttore + MAC** in the details pane: the ARP table gives each dotted-IP device its MAC, and
  the OUI database resolves the manufacturer (when an `oui.txt` is installed - see below).
- **NetBIOS names**: SMB hosts that were only an IP get their real Windows computer name. When a
  host has NetBIOS over TCP/IP disabled (UDP 137 closed or filtered), a reverse-DNS (PTR) lookup is
  tried as a fallback, so a host the resolver knows by name is still labelled instead of a bare IP.
- **SSDP/UPnP devices** are folded into the list under a "UPnP" category, deduped against mDNS/SMB
  by address.
- **Wake-on-LAN**: a context-menu action on any device we have a MAC for. Learned MACs are persisted
  to `~/config/settings/Campiello/won_macs`, so a device can still be woken after it has gone to
  sleep and dropped out of the ARP table.

Enrichment is applied before the service-list signature is hashed, so it triggers exactly one list
rebuild when it arrives and then stays stable (no flicker).

## The OUI data file (manufacturer names)

The IEEE `oui.txt` is not bundled (it is ~6 MB and updated often). `FindOuiFile()` looks for it, in
order, in:

1. `~/config/settings/Campiello/oui.txt`
2. `~/config/data/Campiello/oui.txt`
3. `/system/data/Campiello/oui.txt`

If none is present the manufacturer line is simply omitted (zero-config: absence is silent). To turn
manufacturer names on, download the IEEE registry `oui.txt` and drop it at the first path above.

## Attribution

Ported from LANterna (MIT, atomozero): `src/net/ArpCache.cpp`, `src/net/WakeOnLan.cpp`,
`src/enrich/OuiDatabase.cpp`, `src/enrich/NetBiosEnricher.cpp`, `src/enrich/SsdpEnricher.cpp`.
