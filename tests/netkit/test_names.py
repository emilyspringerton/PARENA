"""stdlib/netkit/nk_names.prn: the maker table, private MACs, name ranks and tidying, the user's labels, the Pi, and the two UDP
name protocols (mDNS reverse lookup, NetBIOS node status) -- every reply parsed here was built independently in Python, byte by
byte, the way a real responder lays it out (RFC 1035/6762 for mDNS, RFC 1002 4.2.18 for NBSTAT)."""
import os, sys, struct, ipaddress
sys.path.insert(0, os.path.dirname(__file__))
from nkharness import *

nk = NK(sys.argv[1])
HERE = os.path.dirname(os.path.abspath(__file__))
OUIBIN = sys.argv[2] if len(sys.argv) > 2 else None
if OUIBIN is None:   # pack the checked-in table the way the page build does
    import subprocess, tempfile
    OUIBIN = os.path.join(tempfile.mkdtemp(), "oui.bin")
    subprocess.run([sys.executable, os.path.join(HERE, "../../tools/netkit_oui_gen.py"), "bin",
                    os.path.join(HERE, "../../stdlib/netkit/data/oui_brands.tsv"), OUIBIN], check=True, stdout=subprocess.DEVNULL)
PKT, LABEL, OUI = 0x5B000, 0x5C000, 0x70000
def u(x): return x & 0xFFFFFFFF
def s32(x): return x - (1 << 32) if x >= (1 << 31) else x
def ipn(t): return int(ipaddress.IPv4Address(t))
def text(t): b = t.encode() if isinstance(t, str) else t; nk.write(TXT, b); return len(b)
PORTS = [22, 80, 443, 445, 3389, 5900, 5901, 8080]
def fresh():
    nk.nk_scan_reset(); nk.nk_scan_begin(text("192.168.4.0/24"))
def found(host, port=22, code=0):
    return nk.nk_scan_result((host - 1) * 8 + PORTS.index(port), code, 3)
def arp(ip, mac):
    line = ("%s 0x1 0x2 %s * wlan0\n" % (ip, mac)).encode(); nk.write(TXT, line); return nk.nk_scan_arp(len(line))
def name(i): return nk.cstr(nk.nk_dev_name_addr(i))
def title(i): return nk.cstr(nk.nk_dev_title_addr(i))
def maker(i): return nk.cstr(nk.nk_dev_maker_text_addr(i))
def guess(i): return nk.cstr(nk.nk_guess_label_addr(nk.nk_dev_guess(i)))

# ---- before the table is loaded ------------------------------------------------------------------------------------------------------
fresh(); i = found(10); arp("192.168.4.10", "b8:27:eb:01:02:03")
check(nk.nk_oui_count() == 0 and maker(i) == "", "no table loaded: no maker, never a wrong one")
check(guess(i) == "Raspberry Pi" and nk.nk_dev_is_pi(i) == 1, "a Pi is still recognised from its prefix without the table")
for mac in ["88:a2:9e:00:00:01", "98:fe:54:00:00:01", "3a:35:41:00:00:01", "2c:cf:67:00:00:01"]:
    fresh(); i = found(11); arp("192.168.4.11", mac)
    check(nk.nk_dev_is_pi(i) == 1, f"Raspberry Pi block {mac[:8]} is recognised (three were missing before)")

# ---- the table ------------------------------------------------------------------------------------------------------------------------------
check(nk.nk_oui_load(16) == 0, "an empty / garbage blob is refused")
blob = open(OUIBIN, "rb").read()
check(len(blob) <= nk.nk_oui_cap(), f"oui.bin ({len(blob)} bytes) fits the 64 KB region")
nk.write(nk.nk_oui_addr(), blob)
n = nk.nk_oui_load(len(blob))
want = {}
for line in open(os.path.join(HERE, "../../stdlib/netkit/data/oui_brands.tsv"), encoding="utf-8"):
    if line.startswith("#") or not line.strip(): continue
    p, b = line.rstrip("\n").split("\t"); want[int(p.replace(":", ""), 16)] = b
check(n == len(want) and n > 10000, f"the table loads: {n} prefixes")
# every single entry round-trips (exhaustive, not sampled)
bad = 0
for p, b in want.items():
    if nk.cstr(nk.nk_maker_name_addr(nk.nk_oui_lookup(p))) != b: bad += 1
check(bad == 0, f"all {len(want)} prefixes look up to their maker")
miss = [p for p in (0x000000, 0xFFFFFF, 0x123456, 0x00000C - 1) if p not in want]
check(all(nk.nk_oui_lookup(p) == 0 for p in miss), "prefixes the table does not list give maker 0 (shown as nothing)")
check(nk.nk_oui_lookup(0xB827EB) != 0 and nk.cstr(nk.nk_maker_name_addr(nk.nk_oui_lookup(0xB827EB))) == "Raspberry Pi", "b8:27:eb -> Raspberry Pi")
check(nk.cstr(nk.nk_maker_name_addr(0)) == "" and nk.cstr(nk.nk_maker_name_addr(9999)) == "", "maker 0 and out-of-range makers are empty")

fresh(); i = found(12); arp("192.168.4.12", "f0:18:98:aa:bb:cc")
check(maker(i) == "Apple", "an ARP-learned MAC shows its maker (f0:18:98 -> Apple)")
fresh(); i = found(13); arp("192.168.4.13", "da:a1:19:00:00:01")
check(nk.nk_dev_mac_private(i) == 1 and maker(i).startswith("Private Wi-Fi address"), "a locally-administered MAC is called private, not unknown")
fresh(); i = found(14)
check(maker(i) == "" and nk.nk_dev_has_mac(i) == 0, "no MAC (Android 10+ hides ARP): no maker line")

# ---- names: tidying and ranks --------------------------------------------------------------------------------------------------------------
fresh(); i = found(20)
for raw, shown in [("raspberrypi.local.", "raspberrypi"), ("printer.home.arpa", "printer"), ("NAS.LAN", "NAS"),
                   ("box.localdomain", "box"), ("tv.fritz.box", "tv"), ("pi.example.com", "pi.example.com"), (".local", ".local")]:
    fresh(); i = found(20); nk.nk_dev_set_name(i, text(raw))
    check(name(i) == shown, f"name {raw!r} is shown as {shown!r}")
fresh(); i = found(20); nk.nk_dev_set_name(i, text("x" * 60))
check(name(i) == "x" * 31, "a long name is cut to 31 bytes")
fresh(); i = found(20); nk.nk_dev_set_name(i, text(b"a\x01b\x1bc"))
check(name(i) == "a?b?c", "control bytes in a name become '?'")
fresh(); i = found(20); nk.nk_dev_set_name(i, text("router-given"))
check(nk.nk_dev_name_rank(i) == 1, "reverse DNS is rank 1")

# ---- mDNS --------------------------------------------------------------------------------------------------------------------------------------
def qname(labels):
    return b"".join(bytes([len(l)]) + l.encode() for l in labels) + b"\0"
ip = s32(ipn("192.168.4.21"))
fresh(); i = found(21)
ln = nk.nk_mdns_query(ip); q = nk.read(PKT, ln)
qid = struct.unpack(">H", q[:2])[0]
expect = struct.pack(">HHHHHH", qid, 0, 1, 0, 0, 0) + qname(["21", "4", "168", "192", "in-addr", "arpa"]) + struct.pack(">HH", 12, 1)
check(q == expect, "mDNS query is a standard PTR question for 21.4.168.192.in-addr.arpa")
check(qid != 0 and qid == ((ipn("192.168.4.21") & 0xFFFF) | 1), "query id derived from the address, never 0")
check(len(nk.read(PKT, nk.nk_mdns_query(s32(ipn("10.0.0.5"))))) == 12 + len(qname(["5", "0", "0", "10", "in-addr", "arpa"])) + 4, "one-digit octets encode too")

def mdns_reply(target, flags=0x8400, with_question=False, compress=True, extra_first=False):
    qn = qname(["21", "4", "168", "192", "in-addr", "arpa"])
    body = b""; qd = 0
    if with_question: body += qn + struct.pack(">HH", 12, 1); qd = 1
    ans = []
    if extra_first:  # an unrelated A record before the PTR
        ans.append(qname(["other", "local"]) + struct.pack(">HHIH", 1, 0x8001, 120, 4) + bytes([192, 168, 4, 21]))
    if compress and with_question:
        owner = b"\xc0\x0c"
    else:
        owner = qn
    # PTR rdata: "raspberrypi" + pointer to a "local" label placed in the rdata itself when compressing
    rd = qname(target.split("."))
    ans.append(owner + struct.pack(">HHIH", 12, 0x8001, 120, len(rd)) + rd)
    return struct.pack(">HHHHHH", qid, flags, qd, len(ans), 0, 0) + body + b"".join(ans)

for desc, pkt, nm in [
    ("plain answer", mdns_reply("raspberrypi.local"), "raspberrypi"),
    ("answer after an echoed question, owner compressed", mdns_reply("raspberrypi.local", with_question=True), "raspberrypi"),
    ("PTR after an unrelated A record", mdns_reply("kitchen-speaker.local", extra_first=True), "kitchen-speaker"),
]:
    fresh(); i = found(21)
    nk.write(PKT, pkt)
    check(nk.nk_mdns_reply(i, len(pkt)) == 1 and name(i) == nm and nk.nk_dev_name_rank(i) == 3, f"mDNS {desc} -> {nm!r}")
check(nk.nk_dev_is_pi(i) == 0, "kitchen-speaker is not a Pi")
fresh(); i = found(21); p = mdns_reply("raspberrypi.local"); nk.write(PKT, p); nk.nk_mdns_reply(i, len(p))
check(nk.nk_dev_is_pi(i) == 1 and guess(i) == "Raspberry Pi", "the name raspberrypi makes it the Pi even with no MAC (a modern tablet)")
# rdata that uses a compression pointer back into the packet
fresh(); i = found(22)
head = struct.pack(">HHHHHH", qid, 0x8400, 0, 1, 0, 0)
owner = qname(["22", "4", "168", "192", "in-addr", "arpa"])
local_at = len(head) + len(owner) + 10 + 7   # where "local" will sit inside the rdata of a first label "studio"
rd = b"\x06studio" + b"\xc0" + bytes([0])   # placeholder pointer fixed below
pk = head + owner + struct.pack(">HHIH", 12, 0x8001, 120, 0) + b"\x05local\x00"   # write "local" first at a known offset
off_local = len(head) + len(owner) + 10
rd = b"\x06studio" + struct.pack(">H", 0xC000 | off_local)
pk = head + owner + struct.pack(">HHIH", 12, 0x8001, 120, len(rd)) + rd
pk = pk[:off_local] + rd  # rdata begins at off_local; make "local" reachable by pointing at a second copy appended
tail_at = len(pk)
rd = b"\x06studio" + struct.pack(">H", 0xC000 | tail_at)
pk = head + owner + struct.pack(">HHIH", 12, 0x8001, 120, len(rd)) + rd + b"\x05local\x00"
nk.write(PKT, pk)
check(nk.nk_mdns_reply(i, len(pk)) == 1 and name(i) == "studio", "PTR rdata with a compression pointer decodes (studio.local)")
# hostile / broken replies are refused, never crash, never store
loop_pkt = head + owner + struct.pack(">HHIH", 12, 0x8001, 120, 2) + struct.pack(">H", 0xC000 | (len(head) + len(owner) + 10))
for desc, pkt in [
    ("a query, not a response", mdns_reply("evil.local", flags=0x0000)),
    ("an error response", mdns_reply("evil.local", flags=0x8403)),
    ("truncated header", b"\x00\x01\x84"),
    ("answer count larger than the packet", struct.pack(">HHHHHH", qid, 0x8400, 0, 50, 0, 0) + b"\x00"),
    ("a pointer loop", loop_pkt),
    ("rdlength past the end", head + owner + struct.pack(">HHIH", 12, 0x8001, 120, 400) + b"\x03abc"),
]:
    fresh(); i = found(23)
    nk.write(PKT, pkt)
    check(nk.nk_mdns_reply(i, len(pkt)) == 0 and name(i) == "", f"mDNS refuses {desc}")
check(nk.nk_mdns_reply(999, 40) == 0 and nk.nk_mdns_reply(-1, 40) == 0, "mDNS reply for a device that does not exist is ignored")

# ---- NetBIOS ---------------------------------------------------------------------------------------------------------------------------------------
ip = s32(ipn("192.168.4.30"))
fresh(); i = found(30, 445)
ln = nk.nk_nbns_query(ip); q = nk.read(PKT, ln)
wild = b"\x20" + b"CK" + b"AA" * 15 + b"\x00"
check(ln == 50 and q[2:12] == struct.pack(">HHHHH", 0, 1, 0, 0, 0) and q[12:46] == wild and q[46:50] == struct.pack(">HH", 0x21, 1),
      "NetBIOS node-status query for '*' (RFC 1002 4.2.17)")
def nb_reply(names, mac, flags=0x8400):
    rr = b"".join(n.encode().ljust(15, b" ") + bytes([suf]) + struct.pack(">H", fl) for n, suf, fl in names)
    rd = bytes([len(names)]) + rr + mac + b"\0" * 40
    return struct.pack(">HHHHHH", 0x1234, flags, 0, 1, 0, 0) + wild + struct.pack(">HHIH", 0x21, 1, 0, len(rd)) + rd
names = [("WORKGROUP", 0x00, 0x8400), ("DESKTOP-7Q2", 0x00, 0x0400), ("DESKTOP-7Q2", 0x20, 0x0400)]
p = nb_reply(names, bytes.fromhex("f01898aabbcc")); nk.write(PKT, p)
check(nk.nk_nbns_reply(i, len(p)) == 1 and name(i) == "DESKTOP-7Q2", "NetBIOS: the unique workstation name, not the group (WORKGROUP)")
check(nk.nk_dev_has_mac(i) == 1 and maker(i) == "Apple" and nk.nk_dev_name_rank(i) == 2, "NetBIOS: the MAC it reports gives the maker without ARP")
fresh(); i = found(31, 445)
p = nb_reply([("RASPBERRYPI", 0x00, 0x0400)], b"\0" * 6); nk.write(PKT, p)
check(nk.nk_nbns_reply(i, len(p)) == 1 and nk.nk_dev_has_mac(i) == 0 and nk.nk_dev_is_pi(i) == 1, "Samba's all-zero MAC is ignored; RASPBERRYPI is still the Pi")
fresh(); i = found(32, 445); arp("192.168.4.32", "b8:27:eb:00:00:09")
p = nb_reply([("PC", 0x00, 0x0400)], bytes.fromhex("f01898aabbcc")); nk.write(PKT, p); nk.nk_nbns_reply(i, len(p))
check(maker(i) == "Raspberry Pi", "a MAC already learned from ARP is not replaced by the NetBIOS one")
fresh(); i = found(33, 445)
p = nb_reply(names, bytes.fromhex("f01898aabbcc"))[:60]; nk.write(PKT, p)
check(nk.nk_nbns_reply(i, len(p)) == 0 and name(i) == "", "a truncated NetBIOS reply is refused")
p = nb_reply(names, bytes.fromhex("f01898aabbcc"), flags=0x0000); nk.write(PKT, p)
check(nk.nk_nbns_reply(i, len(p)) == 0, "a NetBIOS query (not a response) is refused")

# ranks: mDNS beats NetBIOS beats reverse DNS, whatever order they arrive in
fresh(); i = found(40, 445)
p = nb_reply([("STUDIO-PC", 0x00, 0x0400)], b"\0" * 6); nk.write(PKT, p); nk.nk_nbns_reply(i, len(p))
nk.nk_dev_set_name(i, text("192-168-4-40.isp.example"))
check(name(i) == "STUDIO-PC", "reverse DNS arriving later does not replace a NetBIOS name")
p = mdns_reply("studio.local"); nk.write(PKT, p); nk.nk_mdns_reply(i, len(p))
check(name(i) == "studio", "mDNS replaces a NetBIOS name")
p = nb_reply([("OTHER", 0x00, 0x0400)], b"\0" * 6); nk.write(PKT, p); nk.nk_nbns_reply(i, len(p))
check(name(i) == "studio", "...and a later NetBIOS reply does not take it back")

# ---- the user's label, the title, the Pi first ----------------------------------------------------------------------------------------------------
fresh(); a = found(50); b = found(9); c = found(70)
p = mdns_reply("raspberrypi.local"); nk.write(PKT, p); nk.nk_mdns_reply(c, len(p))
check(title(a) == "" , "no name and no label: an empty title (the page shows the address)")
nk.nk_dev_set_name(a, text("tv.lan")); check(title(a) == "tv", "a discovered name is the title")
nk.nk_dev_set_label(a, text("Living room TV")); check(title(a) == "Living room TV" and nk.nk_dev_has_label(a) == 1, "the user's label beats the discovered name")
check(name(a) == "tv", "...and the discovered name is kept underneath it")
nk.nk_dev_set_label(a, 0); check(title(a) == "tv" and nk.nk_dev_has_label(a) == 0, "an empty label clears it")
nk.nk_dev_set_label(a, text("y" * 50)); check(title(a) == "y" * 31, "a label is cut to 31 bytes")
nk.nk_dev_sort()
order = [u(nk.nk_dev_ip(nk.nk_dev_order(k))) for k in range(nk.nk_dev_count())]
check(order == [ipn("192.168.4.70"), ipn("192.168.4.9"), ipn("192.168.4.50")], "the Raspberry Pi sorts to the top, the rest by address")
nk.nk_dev_set_pinned(a, 1); nk.nk_dev_sort()
check(u(nk.nk_dev_ip(nk.nk_dev_order(0))) == ipn("192.168.4.50") and u(nk.nk_dev_ip(nk.nk_dev_order(1))) == ipn("192.168.4.70"), "a pinned device still beats the Pi")
done()
