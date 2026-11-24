"""nk_rfb.prn against a REAL VNC server (libvncserver, TightVNC's lineage -- see vncd.c). Everything in test_rfb.py is
checked against a fake server this repo wrote, which can only prove the module agrees with its author. This test is the
independent one: real VNC authentication, the server's own encoder (Hextile/CopyRect/DesktopSize), real Bell and
ServerCutText, and the server's own final framebuffer compared byte for byte with what the wasm client rebuilt.

usage: test_rfb_interop.py <netkit.wasm> <vncd binary> [libdir]    (skips cleanly if the server isn't available)
"""
import os, socket, subprocess, sys, time
sys.path.insert(0, os.path.dirname(__file__))
from nkharness import *

if len(sys.argv) < 3 or not os.path.exists(sys.argv[2]):
    print("SKIP: no vncd test server (build tests/netkit/vncd.c against libvncserver)"); sys.exit(0)
nk = NK(sys.argv[1]); VNCD = sys.argv[2]
env = dict(os.environ)
if len(sys.argv) > 3: env["LD_LIBRARY_PATH"] = sys.argv[3] + ":" + env.get("LD_LIBRARY_PATH", "")
PORT = 5977 + (os.getpid() % 500) * 2

def start(pw, rgb):
    p = subprocess.Popen([VNCD, str(PORT), pw, rgb], env=env, stderr=subprocess.PIPE, stdout=subprocess.DEVNULL)
    for _ in range(100):
        try: socket.create_connection(("127.0.0.1", PORT), 0.2).close(); break
        except OSError: time.sleep(0.05)
    time.sleep(0.2)
    return p

def session(password, rgb, pw_on_server, max_seconds=12):
    """Drive the wasm client over a real socket until the server closes. Returns events."""
    srv = start(pw_on_server, rgb)
    nk.write(PW, password.ljust(16, b"\0")) if password else None
    nk.nk_rfb_begin(len(password))
    sock = socket.create_connection(("127.0.0.1", PORT)); sock.settimeout(0.3)
    gens = {nk.nk_rfb_gen()}; t0 = time.time(); sent_total = b""; closed = False
    while time.time() - t0 < max_seconds and nk.nk_rfb_phase() not in (8, 9):
        try: data = sock.recv(65536)
        except socket.timeout: data = None
        except ConnectionError: data = b""
        if data == b"": nk.nk_rfb_closed(); closed = True; break
        if data:
            for i in range(0, len(data), 40000):
                chunk = data[i:i + 40000]; p = nk.nk_in_reserve(); nk.write(p, chunk); nk.nk_rfb_feed(len(chunk))
        n = nk.nk_out_len()
        if n: b = nk.read(OUT, n); nk.nk_out_clear(); sent_total += b; sock.sendall(b)
        gens.add(nk.nk_rfb_gen())
    sock.close()
    try: srv.wait(timeout=8)
    except subprocess.TimeoutExpired: srv.kill()
    return srv, gens, sent_total, closed

# ---- 1. password-protected server, correct password: the whole show -------------------------------------------------------------------
rgb = "/tmp/nk_interop_final.rgb"
if os.path.exists(rgb): os.remove(rgb)
srv, gens, sent, closed = session(b"secret", rgb, "secret")
check(nk.nk_err_kind() == 0, "real server, correct password: VNC authentication succeeds (no error)")
check(nk.cstr(NAME) == "vncd-interop", "real server: desktop name read from ServerInit")
check(nk.nk_rfb_frames() >= 4, f"real server: {nk.nk_rfb_frames()} FramebufferUpdates decoded without a protocol error")
check(len(gens) >= 2 and nk.nk_rfb_width() == 256 and nk.nk_rfb_height() == 160, "real server: DesktopSize (320x200 -> 256x160) handled")
check(closed and nk.nk_rfb_phase() == 8, "real server: its disconnect is a clean close, not an error")
ok = os.path.exists(rgb)
check(ok, "real server wrote its final framebuffer")
if ok:
    raw = open(rgb, "rb").read(); hdr, _, body = raw.partition(b"\n"); w, h = map(int, hdr.split())
    mine = nk.read(FB, w * h * 4)
    mine_rgb = b"".join(mine[i:i + 3] for i in range(0, len(mine), 4))
    diff = sum(1 for i in range(0, len(body), 3) if body[i:i + 3] != mine_rgb[i:i + 3])
    check((w, h) == (256, 160) and diff == 0, f"real server: final framebuffer matches the server's own, byte for byte ({diff} differing pixels of {w*h})")
    alpha_ok = all(mine[i + 3] == 255 for i in range(0, len(mine), 4))
    check(alpha_ok, "real server: every pixel's alpha is 255")
# the view-only guarantee, on the real wire
types = []
i = 0
while i < len(sent):
    # skip the handshake bytes: greeting 12 + sectype 1 + response 16 + clientinit 1 = 30
    if i == 0: i = 30; continue
    t = sent[i]; types.append(t)
    i += {0: 20, 3: 10}.get(t, 4 + 4 * int.from_bytes(sent[i + 2:i + 4], "big") if t == 2 else 1)
check(set(types) <= {0, 2, 3}, f"real wire, view-only: client sent only message types {sorted(set(types))} (never 4/5/6)")

# ---- 2. wrong password on a real server ---------------------------------------------------------------------------------------------------
srv, gens, sent, closed = session(b"nope", "/tmp/nk_interop_x.rgb", "secret", 6)
check(nk.nk_rfb_phase() == 9 and nk.nk_err_kind() == 4, "real server, wrong password: AUTH_FAILED")
print("      server's own reason text:", repr(nk.cstr(REASON)))

# ---- 3. password required but none saved ---------------------------------------------------------------------------------------------------
srv, gens, sent, closed = session(b"", "/tmp/nk_interop_x.rgb", "secret", 6)
check(nk.nk_rfb_phase() == 9 and nk.nk_err_kind() == 4 and nk.nk_err_msg() == 4, "real server needs a password and none is saved: told so plainly")

# ---- 4. server with no password (Security: None) ---------------------------------------------------------------------------------------------
rgb2 = "/tmp/nk_interop_nopw.rgb"
if os.path.exists(rgb2): os.remove(rgb2)
srv, gens, sent, closed = session(b"", rgb2, "-")
check(nk.nk_err_kind() == 0 and nk.nk_rfb_frames() >= 4, "real server, no password (Security None, 3.8 SecurityResult): works")
check(os.path.exists(rgb2) and open(rgb2, "rb").read().split(b"\n", 1)[1] == b"".join(nk.read(FB, 256 * 160 * 4)[i:i + 3] for i in range(0, 256 * 160 * 4, 4)),
      "real server, no password: final framebuffer matches byte for byte")
done()
