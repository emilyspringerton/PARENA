"""stdlib/netkit/nk_scan.prn: sweep planning, job enumeration, result merging, ARP parsing, guesses, ordering."""
import os, sys, ipaddress
sys.path.insert(0, os.path.dirname(__file__))
from nkharness import *

nk = NK(sys.argv[1])
def u(x): return x & 0xFFFFFFFF
def s32(x): return x - (1 << 32) if x >= (1 << 31) else x
def ipn(t): return int(ipaddress.IPv4Address(t))
def text(t): b = t.encode(); nk.write(TXT, b); return len(b)
def begin(cidr): nk.nk_scan_reset(); return nk.nk_scan_begin(text(cidr))
def jobs():
    out = []
    while True:
        j = nk.nk_scan_next()
        if j < 0: return out
        out.append((u(nk.nk_scan_job_ip(j)), nk.nk_scan_job_port(j), j))
PORTS = [22, 80, 443, 445, 3389, 5900, 5901, 8080]

# ---- planning ------------------------------------------------------------------------------------------------------------------------
check(begin("192.168.4.0/24") == 1 and nk.nk_scan_hosts() == 254 and nk.nk_scan_total() == 254 * 8, "/24: 254 hosts x 8 default ports = 2032 probes")
js = jobs()
check(len(js) == 2032 and len({(a, b) for a, b, _ in js}) == 2032, "enumeration yields every (host,port) exactly once")
want = [(ipn("192.168.4.%d" % h), p) for h in range(1, 255) for p in PORTS]
check([(a, b) for a, b, _ in js] == want, "order is host-major (all ports of .1, then .2, ...), skipping .0 and .255")
check(nk.nk_scan_next() == -1, "after the last probe next() keeps saying -1")
check(nk.nk_scan_truncated() == 0, "a /24 is not truncated")
begin("192.168.4.77/24"); check(u(nk.nk_scan_job_ip(0)) == ipn("192.168.4.1") and nk.nk_scan_hosts() == 254, "an unaligned address is masked to its network")
begin("10.0.0.0/30"); check(nk.nk_scan_hosts() == 2 and [a for a, _, _ in jobs()][::8] == [ipn("10.0.0.1"), ipn("10.0.0.2")], "/30: two usable hosts")
begin("10.0.0.4/31"); check(nk.nk_scan_hosts() == 2 and u(nk.nk_scan_job_ip(0)) == ipn("10.0.0.4"), "/31: two hosts, no network/broadcast carve-out")
begin("172.16.9.9"); check(nk.nk_scan_hosts() == 1 and u(nk.nk_scan_job_ip(0)) == ipn("172.16.9.9"), "bare address = one host")
begin("172.16.9.9/32"); check(nk.nk_scan_hosts() == 1, "/32 = one host")
begin("10.1.0.0/16"); check(nk.nk_scan_hosts() == 1022 and nk.nk_scan_truncated() == 1 and nk.nk_scan_prefix() == 22, "a /16 is clamped to /22 (1022 hosts) and says so")
begin("200.100.50.0/24"); check(u(nk.nk_scan_job_ip(0)) == ipn("200.100.50.1"), "addresses above 128.x.x.x (negative as I32) enumerate correctly")
for bad in ["", "abc", "300.1.1.1/24", "1.2.3/24", "1.2.3.4/33", "1.2.3.4/", "1.2.3.4.5", "1..3.4/24", "192.168.4.0/ 24"]:
    check(begin(bad) == 0 and nk.nk_scan_next() == -1, f"invalid range {bad!r} is refused and plans nothing")

# ---- exclusion + stop ----------------------------------------------------------------------------------------------------------------
begin("192.168.4.0/29"); nk.nk_scan_exclude(s32(ipn("192.168.4.3")))
got = jobs(); check(all(a != ipn("192.168.4.3") for a, _, _ in got) and len(got) == (6 - 1) * 8, "the excluded host (this tablet) is skipped whole")
check(nk.nk_scan_done() == 8 and nk.nk_scan_total() == 48, "...its probes count as done, so the sweep's progress can reach its total")
for a, b, j in got: nk.nk_scan_result(j, 2, 5)
check(nk.nk_scan_done() == 48 and nk.nk_scan_running() == 0, "after every handed-out probe is answered the sweep reads as finished (it used to hang at 40/48)")
nk.nk_scan_clear_exclude()
begin("192.168.4.0/24"); nk.nk_scan_next(); nk.nk_scan_next(); nk.nk_scan_stop()
check(nk.nk_scan_next() == -1 and nk.nk_scan_running() == 0, "stop(): nothing more is handed out and the sweep reads as finished")

# ---- merging results -----------------------------------------------------------------------------------------------------------------------
begin("192.168.4.0/24")
def result(host, port, code, ms=5):
    k = PORTS.index(port); job = (host - 1) * 8 + k
    return nk.nk_scan_result(job, code, ms)
check(nk.nk_dev_count() == 0, "no devices before any result")
check(result(10, 22, 2) == -1 and nk.nk_dev_count() == 0 and nk.nk_scan_timeouts() == 1, "a timeout reveals nothing")
check(result(10, 22, 3) == -1 and nk.nk_dev_count() == 0, "unreachable reveals nothing")
i = result(10, 22, 0, 9)
check(i == 0 and nk.nk_dev_count() == 1 and u(nk.nk_dev_ip(0)) == ipn("192.168.4.10"), "an OPEN port creates the device")
check(nk.nk_dev_has_port(0, 22) == 1 and nk.nk_dev_has_port(0, 5900) == 0 and nk.nk_dev_rtt(0) == 9, "port bit and rtt recorded")
result(10, 5900, 0, 4); check(nk.nk_dev_has_port(0, 5900) == 1 and nk.nk_dev_rtt(0) == 4, "a second open port sets its bit; rtt keeps the best")
check(result(11, 80, 1) == 1 and nk.nk_dev_count() == 2 and nk.nk_dev_ports(1) == 0, "a REFUSED connect (RST) proves the host is up, with no ports open")
check(nk.nk_dev_guess(1) == 7 and nk.cstr(nk.nk_guess_label_addr(7)) == "Device (no open services)", "host that only refuses: 'Device (no open services)'")
check(nk.nk_scan_done() == 5 and nk.nk_scan_opens() == 2, "done / open counters")
check(nk.nk_scan_result(99999, 0, 1) == -1, "a result for a job that does not exist is ignored")

def guess_for(open_ports, mac=None):
    nk.nk_scan_reset(); nk.nk_scan_begin(text("192.168.4.0/24"))
    idx = None
    for p in open_ports: idx = result(20, p, 0)
    if idx is None: idx = result(20, 22, 1)
    if mac:
        nk.write(TXT, ("%s 0x1 0x2 %s * wlan0\n" % ("192.168.4.20", mac)).encode()); nk.nk_scan_arp(len(("%s 0x1 0x2 %s * wlan0\n" % ("192.168.4.20", mac)).encode()))
    return nk.cstr(nk.nk_guess_label_addr(nk.nk_dev_guess(idx)))
check(guess_for([22, 5900]) == "VNC host", "22 + 5900 -> VNC host (a VNC-capable device is labelled by what the tablet can do with it)")
check(guess_for([445, 5900]) == "Windows PC (VNC ready)", "445 + 5900 -> Windows PC (VNC ready)")
check(guess_for([3389]) == "Windows PC", "3389 -> Windows PC")
check(guess_for([5900]) == "VNC host", "5900 alone -> VNC host")
check(guess_for([22]) == "SSH device", "22 alone -> SSH device")
check(guess_for([22, 80]) == "Linux server", "22 + 80 -> Linux server")
check(guess_for([80]) == "Web device / router", "80 alone -> Web device / router")
check(guess_for([22], "b8:27:eb:12:34:56") == "Raspberry Pi", "Raspberry Pi OUI b8:27:eb wins over the port guess")
check(guess_for([22], "DC:A6:32:AA:BB:CC") == "Raspberry Pi", "OUI match is case-insensitive (dc:a6:32)")
check(guess_for([22], "00:11:22:33:44:55") == "SSH device", "an ordinary OUI does not make a Pi")

# ---- ARP -------------------------------------------------------------------------------------------------------------------------------------
begin("192.168.4.0/24")
arp = ("IP address       HW type     Flags       HW address            Mask     Device\n"
       "192.168.4.1      0x1         0x2         b8:27:eb:aa:bb:01     *        wlan0\n"
       "192.168.4.23     0x1         0x2         3C:22:FB:12:34:56     *        wlan0\n"
       "192.168.4.99     0x1         0x0         00:00:00:00:00:00     *        wlan0\n"
       "garbage line here\n"
       "192.168.4.50     0x1         0x2         zz:22:FB:12:34:56     *        wlan0\n"
       "192.168.4.7      0x1         0x2         aa:bb:cc:dd:ee:0f     *        wlan0")
n = nk.nk_scan_arp(text(arp))
check(n == 3 and nk.nk_dev_count() == 3, "ARP: the header, an incomplete entry (zero MAC), garbage and a bad-hex MAC are skipped; 3 real hosts found (last line has no newline)")
ips = sorted(u(nk.nk_dev_ip(k)) for k in range(3)); check(ips == sorted([ipn("192.168.4.1"), ipn("192.168.4.23"), ipn("192.168.4.7")]), "ARP-only hosts appear (they never answered a port)")
k1 = [k for k in range(3) if u(nk.nk_dev_ip(k)) == ipn("192.168.4.1")][0]
check(nk.nk_dev_has_mac(k1) == 1 and nk.read(nk.nk_dev_mac_addr(k1), 6) == bytes.fromhex("b8 27 eb aa bb 01".replace(" ", "")), "MAC bytes stored")
check(nk.cstr(nk.nk_guess_label_addr(nk.nk_dev_guess(k1))) == "Raspberry Pi", "an ARP-only Pi is recognised by its MAC alone")
check(nk.nk_scan_arp(text(arp)) == 3 and nk.nk_dev_count() == 3, "re-reading the ARP table does not duplicate devices")
j = result(23, 445, 0); k23 = [k for k in range(nk.nk_dev_count()) if u(nk.nk_dev_ip(k)) == ipn("192.168.4.23")][0]
check(j == k23 and nk.nk_dev_has_mac(k23) == 1 and nk.nk_dev_has_port(k23, 445) == 1, "a scan result merges into the ARP-known device (same record)")

# ---- names, pinning, ordering -------------------------------------------------------------------------------------------------------------------
nk.write(TXT, b"imac.lan"); nk.nk_dev_set_name(k23, 8); check(nk.cstr(nk.nk_dev_name_addr(k23)) == "imac.lan", "device name stored")
nk.write(TXT, b"x" * 60); nk.nk_dev_set_name(k23, 60); check(len(nk.cstr(nk.nk_dev_name_addr(k23))) == 31, "an over-long name is cut to 31 bytes, never overruns the record")
nk.nk_scan_reset(); nk.nk_scan_begin(text("0.0.0.0/22"))
for host in ("192.168.4.5", "10.0.0.2", "200.1.1.1", "128.0.0.9", "9.9.9.9"):
    nk.write(TXT, ("%s 0x1 0x2 aa:bb:cc:00:00:01 * x\n" % host).encode()); nk.nk_scan_arp(len(("%s 0x1 0x2 aa:bb:cc:00:00:01 * x\n" % host).encode()))
nk.nk_dev_sort(); order = [u(nk.nk_dev_ip(nk.nk_dev_order(k))) for k in range(5)]
check(order == sorted(order), "display order is numeric by address, unsigned (9.x, 10.x, 128.x, 192.x, 200.x -- not sign-flipped)")
pin = [k for k in range(5) if u(nk.nk_dev_ip(k)) == ipn("200.1.1.1")][0]; nk.nk_dev_set_pinned(pin, 1); nk.nk_dev_sort()
check(u(nk.nk_dev_ip(nk.nk_dev_order(0))) == ipn("200.1.1.1") and nk.nk_dev_pinned(pin) == 1, "a pinned device sorts first")
nk.nk_dev_set_pinned(pin, 0); nk.nk_dev_sort(); check(nk.nk_dev_pinned(pin) == 0 and u(nk.nk_dev_ip(nk.nk_dev_order(0))) == ipn("9.9.9.9"), "unpinning restores address order")

# ---- limits ------------------------------------------------------------------------------------------------------------------------------------------
nk.nk_scan_reset(); nk.nk_scan_begin(text("10.0.0.0/22"))
made = 0
for h in range(1, 1023):
    for pp in (22,):
        r = nk.nk_scan_result((h - 1) * 8, 0, 1)
        if r >= 0: made += 1
for h in range(1023, 1100):
    nk.nk_scan_result(0, 0, 1)
check(nk.nk_dev_count() <= 512 and made == 512, "the device table caps at 512 and then drops, not crashes")
nk.nk_scan_reset(); nk.nk_scan_begin(text("192.168.4.0/24"))
nk.write(nk.nk_ports_addr(), b"".join((p).to_bytes(4, "little") for p in (22, 5900, 9999))); nk.nk_ports_set(3)
check(nk.nk_nports() == 3 and nk.nk_port_at(2) == 9999, "the port list can be replaced (22, 5900, 9999)")
nk.nk_scan_begin(text("192.168.4.0/30")); check(nk.nk_scan_total() == 2 * 3, "a new plan uses the replaced port list")

# ---- vocabulary --------------------------------------------------------------------------------------------------------------------------------------------
check([nk.nk_conn_kind(c) for c in range(5)] == [0, 2, 3, 1, 6], "connect codes map to ErrorKind: open->NONE, refused->PORT_CLOSED, timeout->TIMEOUT, unreachable->UNREACHABLE, other->UNKNOWN")
check(nk.cstr(nk.nk_err_name_addr(2)) == "PORT_CLOSED" and nk.cstr(nk.nk_err_text_addr(3)) == "No answer (asleep, or blocked?)", "ErrorKind names and the plain-words sentences come from PARENA")
check(nk.cstr(nk.nk_service_name_addr(22)) == "SSH" and nk.cstr(nk.nk_service_name_addr(5900)) == "VNC" and nk.cstr(nk.nk_service_name_addr(1234)) == "", "service names")
done()
