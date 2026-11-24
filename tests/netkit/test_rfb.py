"""stdlib/netkit/nk_rfb.prn against a scripted fake VNC server. Every scenario is also run with the server's bytes
fed one at a time and in random chunks, because the parser is a sans-IO state machine that must not care how TCP
slices the stream. Hextile expectations come from an INDEPENDENT encoder written here from the RFC text.
"""
import os, random, struct, sys, time
sys.path.insert(0, os.path.dirname(__file__))
from nkharness import *
from Crypto.Cipher import DES

nk = NK(sys.argv[1])
R = random.Random(20261124)

def u8(v): return bytes([v & 255])
def u16(v): return struct.pack(">H", v)
def u32(v): return struct.pack(">I", v & 0xFFFFFFFF)
def i32(v): return struct.pack(">i", v)

def feed(data, mode="whole"):
    """Deliver bytes the way the page host does: reserve, write, feed. mode: whole | byte | random."""
    data = bytes(data); i = 0
    while i < len(data):
        n = len(data) - i if mode == "whole" else (1 if mode == "byte" else R.randint(1, 97))
        n = min(n, len(data) - i, nk.nk_in_space(), 60000)
        p = nk.nk_in_reserve()
        nk.write(p, data[i:i + n]); nk.nk_rfb_feed(n); i += n

def out(clear=True):
    n = nk.nk_out_len(); b = nk.read(OUT, n)
    if clear: nk.nk_out_clear()
    return b

def begin(password=b""):
    nk.write(PW, password.ljust(16, b"\0")[:16] if password else b"")
    nk.nk_rfb_begin(len(password))

def fb(w, h): return nk.read(FB, w * h * 4)
def px(r, g, b): return bytes([r, g, b, 255])

PIXFMT = bytes(16)
def server_init(w, h, name=b"TEST-IMAC"): return u16(w) + u16(h) + PIXFMT + u32(len(name)) + name
def fbu(*rects): return u8(0) + u8(0) + u16(len(rects)) + b"".join(rects)
def rect(x, y, w, h, enc, data=b""): return u16(x) + u16(y) + u16(w) + u16(h) + i32(enc) + data

def vnc_response(challenge, password):
    key = bytes(int(f"{b:08b}"[::-1], 2) for b in password.ljust(8, b"\0")[:8])
    c = DES.new(key, DES.MODE_ECB)
    return c.encrypt(challenge[:8]) + c.encrypt(challenge[8:16])

def parse_client_messages(b):
    """Split everything the client sent into (type, ...) and fail on anything but SetPixelFormat/SetEncodings/FBUR."""
    msgs = []; i = 0
    while i < len(b):
        t = b[i]
        if t == 0: msgs.append(("setpixelformat", b[i:i + 20])); i += 20
        elif t == 2:
            n = struct.unpack(">H", b[i + 2:i + 4])[0]
            msgs.append(("setencodings", [struct.unpack(">i", b[i + 4 + 4 * k:i + 8 + 4 * k])[0] for k in range(n)])); i += 4 + 4 * n
        elif t == 3: msgs.append(("fbur", b[i + 1], struct.unpack(">HHHH", b[i + 2:i + 10]))); i += 10
        else: msgs.append(("FORBIDDEN", t)); i += 1
    return msgs

def handshake38_auth(pw=b"secret", mode="whole", size=(64, 48)):
    begin(pw)
    feed(b"RFB 003.008\n", mode)
    sent = out()
    feed(u8(1) + u8(2), mode)
    sent += out()
    chal = bytes(R.randrange(256) for _ in range(16))
    feed(chal, mode); resp = out(); sent += resp
    feed(u32(0), mode); sent += out()
    feed(server_init(*size), mode); sent += out()
    return sent, chal, resp

# ---- 1. full 3.8 + VNC auth handshake, every delivery mode ------------------------------------------------------------
for mode in ("whole", "byte", "random"):
    sent, chal, resp = handshake38_auth(mode=mode)
    check(sent.startswith(b"RFB 003.008\n\x02"), f"[{mode}] client greets RFB 003.008 and picks VNC auth (2)")
    check(resp == vnc_response(chal, b"secret"), f"[{mode}] auth response = DES(bit-reversed key, challenge), 16 bytes, matches pycryptodome")
    check(nk.nk_rfb_phase() == 3 and nk.nk_rfb_width() == 64 and nk.nk_rfb_height() == 48, f"[{mode}] reaches running state with the server's 64x48 screen")
    check(nk.cstr(NAME) == "TEST-IMAC", f"[{mode}] desktop name captured")
    tail = sent[sent.index(b"\x01", 1 + 12 + 1 + 16) if False else 0:]
    msgs = parse_client_messages(sent[12 + 1 + 16:][1:])   # after greeting(12) + sectype(1) + response(16), drop ClientInit(1)
    check([m[0] for m in msgs] == ["setpixelformat", "setencodings", "fbur"], f"[{mode}] then sends SetPixelFormat, SetEncodings, FramebufferUpdateRequest")
    check(msgs[0][1] == bytes([0, 0, 0, 0, 32, 24, 0, 1, 0, 255, 0, 255, 0, 255, 0, 8, 16, 0, 0, 0]), f"[{mode}] pixel format: 32bpp LE true-colour, R/G/B shifts 0/8/16")
    check(msgs[1][1] == [1, 5, 0, -223], f"[{mode}] encodings: CopyRect, Hextile, Raw, DesktopSize")
    check(msgs[2][1:] == (0, (0, 0, 64, 48)), f"[{mode}] first request is a full, non-incremental one")
    check(sent[12 + 1 + 16] == 1, f"[{mode}] ClientInit asks for a SHARED session (never kicks the other viewer)")

# ---- 2. wrong password -> AUTH_FAILED with the server's own reason --------------------------------------------------------
begin(b"wrong")
feed(b"RFB 003.008\n"); out(); feed(u8(1) + u8(2)); out(); feed(bytes(16)); out()
feed(u32(1) + u32(21) + b"Authentication failed")
check(nk.nk_rfb_phase() == 9 and nk.nk_err_kind() == 4 and nk.nk_err_msg() == 5, "wrong password: failed with AUTH_FAILED / 'Wrong password'")
check(nk.cstr(REASON) == "Authentication failed", "wrong password: the server's own reason text is kept")
check(nk.cstr(nk.nk_err_text_addr(4)) == "Wrong username or password", "AUTH_FAILED has the plain-words sentence from PARENA")
# the same, delivered a byte at a time, with the reason split across feeds
begin(b"wrong")
feed(b"RFB 003.008\n", "byte"); out(); feed(u8(1) + u8(2), "byte"); out(); feed(bytes(16), "byte"); out()
feed(u32(1) + u32(21) + b"Authentication failed", "byte")
check(nk.nk_rfb_phase() == 9 and nk.cstr(REASON) == "Authentication failed", "wrong password, byte-at-a-time: same result")

# ---- 3. other versions and security types -----------------------------------------------------------------------------------
begin()
feed(b"RFB 003.008\n"); out(); feed(u8(1) + u8(1)); sent = out()
check(sent == b"\x01", "3.8 + None offered, no password: picks None (1)")
check(nk.nk_rfb_state() == 5, "3.8 + None: waits for SecurityResult (3.8 sends one even for None)")
feed(u32(0)); feed(server_init(32, 32)); out()
check(nk.nk_rfb_phase() == 3, "3.8 + None: running")

begin()
feed(b"RFB 003.007\n"); sent = out()
check(sent == b"RFB 003.007\n", "server 3.7: client answers 3.7")
feed(u8(1) + u8(1)); out()
check(nk.nk_rfb_state() == 6, "3.7 + None: NO SecurityResult follows, goes straight to ServerInit")
feed(server_init(16, 16)); out(); check(nk.nk_rfb_phase() == 3, "3.7 + None: running")

begin(b"pw")
feed(b"RFB 003.003\n"); sent = out()
check(sent == b"RFB 003.003\n", "server 3.3: client answers 3.3")
feed(u32(2)); chal = bytes(range(16)); feed(chal); resp = out()
check(resp == vnc_response(chal, b"pw"), "3.3: server dictates VNC auth; response correct for a 2-char password")
feed(u32(0)); feed(server_init(16, 16)); out(); check(nk.nk_rfb_phase() == 3, "3.3 + VNC auth: running")

begin()
feed(b"RFB 003.003\n"); out(); feed(u32(1)); out()
check(nk.nk_rfb_state() == 6, "3.3 + None: straight to ServerInit")

begin()
feed(b"RFB 003.889\n"); sent = out()
check(sent == b"RFB 003.008\n", "Apple-style 003.889 is treated as 3.8")

begin(b"x")
feed(b"RFB 003.008\n"); out(); feed(u8(2) + u8(1) + u8(2)); sent = out()
check(sent == b"\x02", "both None and VNC auth offered, password given: uses VNC auth")

begin()
feed(b"RFB 003.008\n"); out(); feed(u8(1) + u8(2))
check(nk.nk_rfb_phase() == 9 and nk.nk_err_kind() == 4 and nk.nk_err_msg() == 4, "VNC auth only, no password saved: AUTH_FAILED 'This server needs a password' (no guessing)")

begin(b"x")
feed(b"RFB 003.008\n"); out(); feed(u8(2) + u8(30) + u8(33))
check(nk.nk_rfb_phase() == 9 and nk.nk_err_kind() == 5 and nk.nk_err_msg() == 3, "only MS-Logon/ARD offered: PROTOCOL 'only offers login methods this app doesn't support'")

begin()
feed(b"RFB 003.008\n"); out(); feed(u8(0) + u32(14) + b"Too many users")
check(nk.nk_rfb_phase() == 9 and nk.cstr(REASON) == "Too many users" and nk.nk_err_msg() == 2, "zero security types: server's reason surfaced")

begin()
feed(b"GET / HTTP/1.1\r\n")
check(nk.nk_rfb_phase() == 9 and nk.nk_err_kind() == 5 and nk.nk_err_msg() == 1, "an HTTP server (not VNC): PROTOCOL 'Not a VNC server (bad greeting)'")

begin()
feed(b"RFB 003.00")
check(nk.nk_rfb_phase() == 1, "a truncated greeting just waits")
nk.nk_rfb_closed()
check(nk.nk_rfb_phase() == 9 and nk.nk_err_kind() == 5, "...and the transport closing mid-handshake is a failure")

# ---- helpers to get a running session ----------------------------------------------------------------------------------------
def running(w, h, mode="whole"):
    begin(); feed(b"RFB 003.008\n", mode); out(); feed(u8(1) + u8(1), mode); out(); feed(u32(0), mode)
    feed(server_init(w, h), mode); out()
    assert nk.nk_rfb_phase() == 3
    nk.nk_rfb_dirty_clear()

def expect_img(w, h, pixels):  # pixels: dict (x,y)->(r,g,b); default black opaque
    img = bytearray(px(0, 0, 0) * (w * h))
    for (x, y), c in pixels.items(): img[(y * w + x) * 4:(y * w + x) * 4 + 4] = px(*c)
    return bytes(img)

# ---- 4. Raw ------------------------------------------------------------------------------------------------------------------
for mode in ("whole", "byte", "random"):
    W, H = 40, 30
    running(W, H, mode)
    rx, ry, rw, rh = 5, 7, 13, 9
    exp = {}; wire = b""
    for j in range(rh):
        for i in range(rw):
            c = (R.randrange(256), R.randrange(256), R.randrange(256)); exp[(rx + i, ry + j)] = c
            wire += bytes(c) + bytes([R.randrange(256)])       # 4th wire byte is junk: must NOT leak into alpha
    feed(fbu(rect(rx, ry, rw, rh, 0, wire)), mode)
    check(fb(W, H) == expect_img(W, H, exp), f"[{mode}] Raw rect decodes exactly (alpha forced to 255 despite junk 4th byte)")
    check(nk.nk_rfb_dirty() == 1 and (nk.nk_rfb_dx0(), nk.nk_rfb_dy0(), nk.nk_rfb_dx1(), nk.nk_rfb_dy1()) == (rx, ry, rx + rw, ry + rh), f"[{mode}] dirty box is exactly the rect")
    m = parse_client_messages(out())
    check(m == [("fbur", 1, (0, 0, W, H))], f"[{mode}] after the update the client asks for the next one, incrementally")
    check(nk.nk_rfb_frames() == 1, f"[{mode}] frame counter advanced")

# ---- 5. Hextile (independent encoder) ----------------------------------------------------------------------------------------
def hextile_encode(img, w, h, rect_w, rect_h):
    """img: dict (x,y)->rgb over the rect. Returns (wire bytes). Exercises raw, solid, fg subrects, coloured subrects,
    and bg/fg persistence across tiles by omitting flags when unchanged."""
    out_b = b""; bg = None; fg = None
    for ty in range(0, rect_h, 16):
        for tx in range(0, rect_w, 16):
            tw, th = min(16, rect_w - tx), min(16, rect_h - ty)
            tile = {(i, j): img[(tx + i, ty + j)] for j in range(th) for i in range(tw)}
            kind = R.choice(["raw", "solid", "fgsub", "colsub", "colsub"])
            if kind == "raw":
                out_b += u8(1) + b"".join(bytes(tile[(i, j)]) + b"\x00" for j in range(th) for i in range(tw)); continue
            # find a bg = the most common colour; subrects = rows runs of other colours (valid for any image)
            counts = {}
            for c in tile.values(): counts[c] = counts.get(c, 0) + 1
            b = max(counts, key=counts.get)
            subs = []
            for j in range(th):
                i = 0
                while i < tw:
                    if tile[(i, j)] == b: i += 1; continue
                    c = tile[(i, j)]; k = i
                    while k < tw and tile[(k, j)] == c and k - i < 16: k += 1
                    subs.append((i, j, k - i, 1, c)); i = k
            flags = 0; body = b""
            if b != bg: flags |= 2; body += bytes(b) + b"\x00"; bg = b
            if subs:
                colours = set(s[4] for s in subs)
                if len(colours) == 1 and kind != "colsub":
                    f = next(iter(colours))
                    if f != fg: flags |= 4; body += bytes(f) + b"\x00"; fg = f
                    flags |= 8; body += u8(len(subs))
                    for (i, j, sw, sh, c) in subs: body += u8(i << 4 | j) + u8((sw - 1) << 4 | (sh - 1))
                else:
                    flags |= 8 | 16; body += u8(len(subs))
                    for (i, j, sw, sh, c) in subs: body += bytes(c) + b"\x00" + u8(i << 4 | j) + u8((sw - 1) << 4 | (sh - 1))
                    # a coloured-subrect tile does not change the fg register
                    fg = fg
            out_b += u8(flags) + body
    return out_b

for mode in ("whole", "byte", "random"):
    W, H = 70, 50
    running(W, H, mode)
    rx, ry, rw, rh = 3, 2, 53, 37                     # not a multiple of 16: partial edge tiles
    palette = [(255, 0, 0), (0, 255, 0), (0, 0, 255), (10, 20, 30), (200, 200, 50), (1, 2, 3)]
    img = {}
    for j in range(rh):
        for i in range(rw):
            img[(i, j)] = palette[0] if (i // 5 + j // 3) % 3 == 0 else (palette[(i // 7 + j // 4) % 6] if R.random() < 0.8 else R.choice(palette))
    wire = hextile_encode(img, rw, rh, rw, rh)
    feed(fbu(rect(rx, ry, rw, rh, 5, wire)), mode)
    exp = {(rx + i, ry + j): c for (i, j), c in img.items()}
    check(fb(W, H) == expect_img(W, H, exp), f"[{mode}] Hextile decodes exactly (raw/solid/subrect/coloured tiles, partial edge tiles, bg+fg persistence)")
    check(nk.nk_rfb_phase() == 3, f"[{mode}] Hextile leaves the session healthy")

# ---- 6. CopyRect, all four overlap directions --------------------------------------------------------------------------------------
W, H = 30, 20
base = {(x, y): (x * 8 % 256, y * 12 % 256, (x * y) % 256) for y in range(H) for x in range(W)}
def paint(mode):
    running(W, H, mode)
    wire = b"".join(bytes(base[(x, y)]) + b"\0" for y in range(H) for x in range(W))
    feed(fbu(rect(0, 0, W, H, 0, wire)), mode); out()
for (sx, sy, dx, dy, w, h, label) in [(2, 2, 5, 4, 12, 9, "down-right overlap"), (5, 4, 2, 2, 12, 9, "up-left overlap"),
                                      (2, 3, 6, 3, 14, 6, "same row, right"), (6, 3, 2, 3, 14, 6, "same row, left"),
                                      (4, 1, 4, 5, 10, 10, "straight down overlap"), (4, 6, 4, 2, 10, 10, "straight up overlap")]:
    for mode in ("whole", "random"):
        paint(mode)
        feed(fbu(rect(dx, dy, w, h, 1, u16(sx) + u16(sy))), mode)
        exp = dict(base)
        snapshot = {(x, y): base[(x, y)] for y in range(H) for x in range(W)}
        for j in range(h):
            for i in range(w): exp[(dx + i, dy + j)] = snapshot[(sx + i, sy + j)]
        check(fb(W, H) == expect_img(W, H, exp), f"[{mode}] CopyRect {label} matches a snapshot-based reference")

# ---- 7. DesktopSize, Bell, ServerCutText, SetColourMapEntries, multi-rect update, several updates in one feed -------------------------
running(20, 10)
gen0 = nk.nk_rfb_gen()
feed(fbu(rect(0, 0, 36, 24, -223)))
check(nk.nk_rfb_width() == 36 and nk.nk_rfb_height() == 24 and nk.nk_rfb_gen() == gen0 + 1, "DesktopSize resizes the framebuffer and bumps the geometry generation")
m = parse_client_messages(out())
check(m and m[-1] == ("fbur", 0, (0, 0, 36, 24)), "after a resize the next request is a FULL one at the new size")
feed(u8(2))                                                    # Bell
feed(u8(3) + bytes(3) + u32(11) + b"hello world")              # ServerCutText
feed(u8(1) + u8(0) + u16(0) + u16(2) + bytes(12))              # SetColourMapEntries, 2 colours
check(nk.nk_rfb_phase() == 3, "Bell, ServerCutText and SetColourMapEntries are consumed harmlessly")
two = fbu(rect(0, 0, 1, 1, 0, bytes([9, 8, 7, 0])), rect(1, 0, 1, 1, 0, bytes([1, 2, 3, 0]))) + fbu(rect(2, 0, 1, 1, 0, bytes([4, 5, 6, 0])))
feed(two, "random")
check(fb(36, 24)[:12] == px(9, 8, 7) + px(1, 2, 3) + px(4, 5, 6), "two updates, three rects, delivered in odd chunks, all land")
check(nk.nk_rfb_frames() >= 3, "frame counter counts completed updates")

# ---- 8. hostile / confused servers ------------------------------------------------------------------------------------------------
running(16, 16)
feed(fbu(rect(10, 10, 10, 10, 0, bytes(400))))
check(nk.nk_rfb_phase() == 9 and nk.nk_err_msg() == 9, "a rect hanging off the screen is refused (PROTOCOL 9), nothing written")
check(fb(16, 16) == px(0, 0, 0) * 256, "...and the framebuffer is untouched")
running(16, 16); feed(fbu(rect(0, 0, 4, 4, 7)))
check(nk.nk_rfb_phase() == 9 and nk.nk_err_msg() == 8, "an encoding we never asked for (7 = Tight) fails cleanly")
running(16, 16); feed(u8(77))
check(nk.nk_rfb_phase() == 9 and nk.nk_err_msg() == 7, "an unknown message type fails cleanly")
running(16, 16); feed(fbu(rect(0, 0, 4, 4, 1, u16(14) + u16(14))))
check(nk.nk_rfb_phase() == 9 and nk.nk_err_msg() == 9, "CopyRect whose source is off-screen is refused")
begin(); feed(b"RFB 003.008\n"); out(); feed(u8(1) + u8(1)); out(); feed(u32(0)); feed(server_init(60000, 60000))
check(nk.nk_rfb_phase() == 9 and nk.nk_err_msg() == 6, "a screen bigger than the framebuffer region is refused with a plain message")
running(16, 16); feed(u8(3) + bytes(3) + u32(0xFFFFFFFF))
check(nk.nk_rfb_phase() == 9, "a 4 GB ServerCutText length cannot make the parser spin or skip forever")
running(16, 16); feed(fbu(rect(0, 0, 16, 16, 5, bytes([1, 0, 0, 0]))))
check(nk.nk_rfb_phase() == 3, "a Hextile tile that has only partly arrived just waits")
# input after a failure is discarded, not parsed
running(16, 16); feed(u8(77)); used = nk.g(3)
feed(bytes(500)); check(nk.nk_rfb_phase() == 9, "bytes after a failure are ignored")

# ---- 9. view-only guarantee: every byte ever sent parses as msg 0 / 2 / 3, never 4 / 5 / 6 --------------------------------------
allsent = b""
for mode in ("whole", "byte", "random"):
    sent, _, _ = handshake38_auth(mode=mode)
    feed(fbu(rect(0, 0, 4, 4, 0, bytes(64))), mode); sent += out()
    feed(u8(3) + bytes(3) + u32(3) + b"abc", mode); sent += out()
    allsent += sent[12 + 1 + 16 + 1:]
msgs = parse_client_messages(allsent)
check(all(m[0] != "FORBIDDEN" for m in msgs) and {m[0] for m in msgs} <= {"setpixelformat", "setencodings", "fbur"},
      "VIEW-ONLY: across full sessions the client emits only SetPixelFormat / SetEncodings / FramebufferUpdateRequest (never key, pointer or clipboard)")

# ---- 10. throughput: a full 1920x1080 raw frame -----------------------------------------------------------------------------------
W, H = 1920, 1080
running(W, H)
frame = bytearray(W * H * 4)
for i in range(0, len(frame), 4 * 97): frame[i:i + 3] = bytes([i % 251, i % 241, i % 239])
msg = fbu(rect(0, 0, W, H, 0, bytes(frame)))
t = time.time()
for i in range(0, len(msg), 60000): feed(msg[i:i + 60000])
dt = time.time() - t
ok = fb(W, H)[0:3] == bytes(frame[0:3]) and nk.nk_rfb_frames() == 1
check(ok, "a full 1920x1080 Raw frame (8.3 MB) decodes")
print(f"      1080p raw frame decoded in {dt*1000:.0f} ms ({len(msg)/1e6/dt:.0f} MB/s) under wasmtime")
check(nk.nk_rfb_dx1() == W and nk.nk_rfb_dy1() == H, "dirty box covers the whole frame")

# ---- 11. clean close while running is not an error --------------------------------------------------------------------------------
running(16, 16); nk.nk_rfb_closed()
check(nk.nk_rfb_phase() == 8 and nk.nk_err_kind() == 0, "a close after the session is running is a clean end, not a failure")
done()
