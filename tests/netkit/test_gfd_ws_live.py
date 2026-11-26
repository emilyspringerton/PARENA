"""The SHANKPHONE's signed-in GoblinFoxDragon route, against production: a real IDUNA token over wss://wotan.okemily.com/GFD/ws
with the app's own Origin (https://netkit.local, which the MUD's GFD_WS_ORIGINS allows) reaches the MUD; a foreign origin is
refused. Needs GFD_SMOKE_IDENTITY=<json with email, password> (the smoke identity lives in ~/secrets, never in a repo); skips without.
usage: test_gfd_ws_live.py
"""
import base64, json, os, re, socket, ssl, struct, sys, urllib.request
ident = os.environ.get("GFD_SMOKE_IDENTITY", "")
if not ident or not os.path.exists(ident):
    print("SKIP: GFD_SMOKE_IDENTITY not set"); sys.exit(0)
d = json.load(open(ident)); fails = 0
def check(c, m):
    global fails
    print(("PASS: " if c else "FAIL: ") + m); fails += 0 if c else 1
req = urllib.request.Request("https://iam.okemily.com/api/v1/auth/email/login", data=json.dumps({"email": d["email"], "password": d["password"]}).encode(),
                             headers={"Content-Type": "application/json", "User-Agent": "Mozilla/5.0"})
r = json.load(urllib.request.urlopen(req, timeout=15)); tok = r.get("access_token") or r.get("token")
check(bool(tok), "IDUNA signs the smoke identity in")
def upgrade(origin):
    s = ssl.create_default_context().wrap_socket(socket.create_connection(("wotan.okemily.com", 443), timeout=10), server_hostname="wotan.okemily.com")
    key = base64.b64encode(os.urandom(16)).decode()
    s.sendall((f"GET /GFD/ws HTTP/1.1\r\nHost: wotan.okemily.com\r\nUser-Agent: Mozilla/5.0\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
               f"Sec-WebSocket-Key: {key}\r\nSec-WebSocket-Version: 13\r\nOrigin: {origin}\r\n\r\n").encode())
    h = b""
    while b"\r\n\r\n" not in h: h += s.recv(1)
    return s, h.split(b"\r\n")[0].decode()
_, line = upgrade("https://evil.example"); check(" 403 " in line, f"a foreign origin is refused ({line})")
s, line = upgrade("https://netkit.local"); check(" 101 " in line, f"the app's origin is upgraded ({line})")
p = json.dumps({"token": tok}).encode(); m = os.urandom(4); q = bytes(b ^ m[i % 4] for i, b in enumerate(p)); n = len(q)
s.sendall(bytes([0x81]) + (bytes([0x80 | n]) if n < 126 else bytes([0xFE]) + struct.pack(">H", n)) + m + q)
s.settimeout(5); out = b""
try:
    while True:
        b = s.recv(4096)
        if not b: break
        out += b
except Exception: pass
text = re.sub(r"\x1b\[[0-9;]*m", "", out.decode("utf-8", "replace"))
check("Authentication required" not in text and "invalid or expired" not in text and len(text) > 10,
      "the token is accepted: the MUD answers (a character, or the first-time name claim)")
print(f"\n{'FAIL' if fails else 'gfd ws ok'} ({fails} failed)"); sys.exit(1 if fails else 0)
