#!/usr/bin/env python3
"""Netkit's manufacturer table (SECTION 646 / DROP1): which company made the network card behind a MAC address.

Two steps, so the product build never needs the network:
  netkit_oui_gen.py brands <wireshark manuf file> <out.tsv>   curate: keep the 24-bit IEEE blocks (MA-L) of the brands a home or
                                                              small-office network actually has, under a short display name
  netkit_oui_gen.py bin <in.tsv> <out oui.bin>                pack the checked-in TSV into the blob the scanner wasm searches

Source: Wireshark's `manuf` (https://www.wireshark.org/download/automated/data/manuf), itself generated from the public IEEE
registries. Curated, not complete: an unlisted maker shows as no maker, never as a wrong one. 28/36-bit blocks (MA-M/MA-S) are left
out -- the wasm matches the first three bytes only.

oui.bin (little-endian, read with load32 by nk_scan.prn):
  0  "NKOU"   4  n entries   8  m brands   12  offset of the name table
  16 n x u32: prefix24 | brand << 24, sorted by prefix24     then m x 24-byte NUL-padded brand names (brand 0 = "" = unknown)
"""
import re, struct, sys

# (display name, regex over the registry's full vendor name). First match wins; order matters for overlaps.
BRANDS = [
    ("Raspberry Pi", r"^Raspberry Pi"),
    ("Apple", r"^Apple, Inc"),
    ("Samsung", r"^Samsung Electro|^SAMSUNG ELECTRO|^Samsung Electronics"),
    ("Google", r"^Google, Inc|^Google LLC|^Nest Labs"),
    ("Amazon", r"^Amazon Technologies|^Amazon\.com"),
    ("Microsoft", r"^Microsoft"),
    ("Intel", r"^Intel Corporate|^Intel Corporation"),
    ("Espressif (smart home)", r"^Espressif"),
    ("Tuya (smart home)", r"^Tuya"),
    ("Sonos", r"^Sonos"),
    ("Roku", r"^Roku"),
    ("LG", r"^LG Electronics|^LG Innotek"),
    ("Sony", r"^Sony Corporation|^Sony Mobile|^Sony Interactive|^Sony Visual|^Sony Home"),
    ("Nintendo", r"^Nintendo"),
    ("Valve", r"^Valve Corporation"),
    ("TP-Link", r"^Tp-Link|^TP-LINK|^TP-Link"),
    ("Netgear", r"^Netgear|^NETGEAR"),
    ("ASUS", r"^ASUSTek|^Asus"),
    ("Ubiquiti", r"^Ubiquiti"),
    ("eero", r"^eero"),
    ("Linksys", r"^Linksys|^Belkin"),
    ("D-Link", r"^D-Link|^D-LINK"),
    ("Cisco", r"^Cisco"),
    ("Arris / CommScope", r"^Commscope|^ARRIS|^Arris"),
    ("Technicolor / Vantiva", r"^Technicolor|^Vantiva"),
    ("Sagemcom", r"^Sagemcom"),
    ("Huawei", r"^Huawei|^HUAWEI"),
    ("Honor", r"^Honor Device"),
    ("Xiaomi", r"^Xiaomi|^Beijing Xiaomi"),
    ("OPPO / Realme / OnePlus", r"Oppo Mobile|^Realme|^OnePlus"),
    ("vivo", r"^vivo Mobile"),
    ("Motorola", r"^Motorola Mobility"),
    ("Lenovo", r"^Lenovo|^LCFC"),
    ("Dell", r"^Dell Inc|^Dell Technologies"),
    ("HP", r"^Hewlett Packard$|^HP Inc|^Hewlett-Packard"),
    ("Acer", r"^Acer "),
    ("Nvidia", r"^NVIDIA|^Nvidia"),
    ("Synology", r"^Synology"),
    ("QNAP", r"^QNAP"),
    ("Brother (printer)", r"^Brother Industries"),
    ("Canon", r"^Canon Inc"),
    ("Epson", r"^Seiko Epson"),
    ("Philips Hue / Signify", r"^Philips Lighting|^Signify"),
    ("Ring", r"^Ring LLC"),
    ("Wyze", r"^Wyze"),
    ("Hikvision (camera)", r"Hikvision"),
    ("Dahua (camera)", r"Dahua"),
    ("Reolink (camera)", r"^Reolink"),
    ("Vizio", r"^Vizio|^VIZIO"),
    ("TCL", r"^TCL |^TCT mobile|^Shenzhen TCL"),
    ("Hisense", r"Hisense"),
    ("Tesla", r"^Tesla"),
    ("Garmin", r"^Garmin"),
    ("Fitbit", r"^Fitbit"),
    ("Bose", r"^Bose Corporation"),
    ("Ecobee", r"^ecobee"),
    ("Arduino", r"^Arduino"),
    ("Texas Instruments", r"^Texas Instruments"),
    ("Silicon Labs", r"^Silicon Laboratories"),
    ("Nordic Semiconductor", r"^Nordic Semiconductor"),
    ("Realtek", r"^Realtek"),
    ("MediaTek", r"^MediaTek|^MEDIATEK"),
    ("Qualcomm", r"^Qualcomm"),
    ("Broadcom", r"^Broadcom"),
    ("Murata (Wi-Fi module)", r"^Murata"),
    ("AzureWave (Wi-Fi chip)", r"^AzureWave"),
    ("Liteon (Wi-Fi module)", r"^Liteon"),
    ("Foxconn (Hon Hai)", r"^Hon Hai|^Cloud Network Technology"),
    ("Pegatron", r"^Pegatron|^PEGATRON"),
    ("Compal", r"^Compal"),
    ("Quanta", r"^Quanta"),
    ("Wistron", r"^Wistron"),
    ("Zyxel", r"^Zyxel|^ZyXEL"),
    ("ZTE", r"^zte|^ZTE"),
    ("Nokia", r"^Nokia"),
    ("Juniper", r"^Juniper"),
    ("Aruba / HPE", r"^Aruba|^Hewlett Packard Enterprise"),
    ("Ruckus", r"^Ruckus"),
    ("Meraki", r"^Cisco Meraki"),
    ("MikroTik", r"^Routerboard|^MikroTik"),
    ("GL.iNet", r"GL Technologies"),
    ("Fortinet", r"^Fortinet"),
    ("VMware (VM)", r"^VMware"),
    ("Parallels (VM)", r"^Parallels"),
    ("QEMU / KVM (VM)", r"^QEMU|^Red Hat"),
    ("Docker (container)", r"^Docker"),
]
# Meraki before Cisco (Cisco would swallow it), Foxconn/Cloud Network = Hon Hai.
BRANDS.sort(key=lambda b: 0 if b[0] == "Meraki" else 1)

def brands(manuf, out):
    rx = [(name, re.compile(pat)) for name, pat in BRANDS]
    rows = []
    for line in open(manuf, encoding="utf-8"):
        if line.startswith("#") or not line.strip():
            continue
        parts = line.rstrip("\n").split("\t")
        prefix = parts[0].strip()
        if "/" in prefix or len(prefix) != 8:
            continue
        full = (parts[2] if len(parts) > 2 else parts[1]).strip()
        for name, r in rx:
            if r.search(full):
                rows.append((prefix.lower(), name))
                break
    rows.sort()
    with open(out, "w", encoding="utf-8") as f:
        f.write("# Netkit manufacturer table -- generated by PARENA/tools/netkit_oui_gen.py from Wireshark's manuf (IEEE MA-L blocks).\n")
        f.write("# prefix<TAB>maker. Regenerate: netkit_oui_gen.py brands <manuf> stdlib/netkit/data/oui_brands.tsv\n")
        for p, n in rows:
            f.write(f"{p}\t{n}\n")
    print(f"{len(rows)} prefixes, {len({n for _, n in rows})} makers -> {out}")

def pack(tsv, out):
    rows = []
    for line in open(tsv, encoding="utf-8"):
        if line.startswith("#") or not line.strip():
            continue
        p, n = line.rstrip("\n").split("\t")
        rows.append((int(p.replace(":", ""), 16), n))
    names = [""] + sorted({n for _, n in rows})
    if len(names) > 255:
        sys.exit("too many makers for one byte")
    idx = {n: i for i, n in enumerate(names)}
    rows.sort()
    prev = -1
    for v, _ in rows:
        if v <= prev:
            sys.exit(f"duplicate or unsorted prefix {v:06x}")
        prev = v
    for n in names:
        if len(n.encode()) > 23:
            sys.exit(f"maker name too long (23 bytes max): {n}")
    head = struct.pack("<4sIII", b"NKOU", len(rows), len(names), 16 + 4 * len(rows))
    body = b"".join(struct.pack("<I", v | idx[n] << 24) for v, n in rows)
    tab = b"".join(n.encode().ljust(24, b"\0") for n in names)
    blob = head + body + tab
    if len(blob) > 65536:
        sys.exit(f"oui.bin is {len(blob)} bytes; the wasm region holds 65536")
    open(out, "wb").write(blob)
    print(f"{out}: {len(rows)} prefixes, {len(names) - 1} makers, {len(blob)} bytes")

if __name__ == "__main__":
    if len(sys.argv) == 4 and sys.argv[1] == "brands":
        brands(sys.argv[2], sys.argv[3])
    elif len(sys.argv) == 4 and sys.argv[1] == "bin":
        pack(sys.argv[2], sys.argv[3])
    else:
        sys.exit(__doc__)
