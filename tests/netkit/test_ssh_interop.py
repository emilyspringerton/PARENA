"""stdlib/netkit/nk_ssh*.prn (a PARENA SSH client, compiled to wasm) against a REAL, independent SSH server implementation:
asyncssh. Nothing here shares code with the client, so a protocol misunderstanding cannot pass by agreeing with itself.

The Python below plays the part of the Android shell: it owns the socket and does exactly what the page host does --
bytes in via nk-in-reserve/nk-ssh-feed, bytes out from OUT, decrypted channel data out of nk-ssh-data-addr.

usage: test_ssh_interop.py <netkit.wasm>
"""
import asyncio, base64, hashlib, logging, os, random, socket, sys, threading, time
logging.getLogger('asyncio').setLevel(logging.CRITICAL)   # asyncssh complaining about peers we drop on purpose
sys.path.insert(0, os.path.dirname(__file__))
from nkharness import *
import asyncssh

nk = NK(sys.argv[1]); R = random.Random(22)

# ---- an asyncssh server on a background event loop --------------------------------------------------------------------------------------------
class Srv:
    def __init__(self, *, kbdint=False, enc=("chacha20-poly1305@openssh.com",), password=("tester", "s3cret"), host_alg="ssh-ed25519"):
        self.password = password; self.kbdint = kbdint; self.enc = list(enc); self.host_alg = host_alg
        self.key = asyncssh.generate_private_key(host_alg)
        self.received = bytearray(); self.resizes = []; self.pty = None; self.logins = 0; self.failures = 0
        self.loop = asyncio.new_event_loop(); self.ready = threading.Event(); self.sessions = []
        threading.Thread(target=self._run, daemon=True).start(); self.ready.wait(10)
    def fingerprint(self): return self.key.get_fingerprint()           # 'SHA256:base64'
    def _run(self):
        asyncio.set_event_loop(self.loop)
        srv = self
        class S(asyncssh.SSHServer):
            def begin_auth(s, username): return True
            def password_auth_supported(s): return not srv.kbdint
            def validate_password(s, username, password):
                ok = (username, password) == srv.password; srv.logins += ok; srv.failures += (not ok); return ok
            def kbdint_auth_supported(s): return srv.kbdint
            def get_kbdint_challenge(s, username, lang, submethods): return ("", "", "", [("Password: ", False)])
            def validate_kbdint_response(s, username, responses):
                ok = (username, responses[0] if responses else "") == srv.password; srv.logins += ok; srv.failures += (not ok); return ok
            def session_requested(s): srv_s = Sess(srv); srv.sessions.append(srv_s); return srv_s
        self.server = self.loop.run_until_complete(asyncssh.create_server(S, "127.0.0.1", 0, server_host_keys=[self.key],
                          encryption_algs=self.enc, kex_algs=["curve25519-sha256", "curve25519-sha256@libssh.org"],
                          mac_algs=["hmac-sha2-256"], compression_algs=["none"]))
        self.port = self.server.sockets[0].getsockname()[1]
        self.ready.set(); self.loop.run_forever()
    def stop(self): self.loop.call_soon_threadsafe(self.server.close)

class Sess(asyncssh.SSHServerSession):
    def __init__(self, srv): self.srv = srv; self.buf = b""
    def connection_made(self, chan): self.chan = chan
    def pty_requested(self, term, size, modes): self.srv.pty = (term, size); return True
    def shell_requested(self): return True
    def session_started(self):
        t, sz = self.srv.pty or ("none", (0, 0, 0, 0))
        self.chan.write(f"WELCOME TERM={t} SIZE={sz[0]}x{sz[1]}\n")
    def terminal_size_changed(self, w, h, pw, ph): self.srv.resizes.append((w, h)); self.chan.write(f"RESIZED {w}x{h}\n")
    def data_received(self, data, dt):
        self.srv.received += data.encode() if isinstance(data, str) else data
        self.buf += data.encode() if isinstance(data, str) else data
        while b"\n" in self.buf:
            line, self.buf = self.buf.split(b"\n", 1); line = line.rstrip(b"\r")
            if line == b"BIG": self.chan.write("".join(chr(65 + (i % 26)) for i in range(300000)) + "\nBIGDONE\n")
            elif line == b"EXIT": self.chan.exit(7)
            elif line == b"STDERR": self.chan.write_stderr("to-stderr\n")
            elif not line.startswith(b"#"): self.chan.write(f"got: {line.decode(errors='replace')}\n")

# ---- a tiny TCP proxy for the "hostile network" scenarios (banner lines, bit flips, chunking) ----------------------------------------------------
class Proxy:
    def __init__(self, port, banner=b"", flip_after=None):
        self.up = port; self.banner = banner; self.flip_after = flip_after
        self.ls = socket.socket(); self.ls.bind(("127.0.0.1", 0)); self.ls.listen(1); self.port = self.ls.getsockname()[1]
        threading.Thread(target=self._run, daemon=True).start()
    def _run(self):
        c, _ = self.ls.accept(); u = socket.create_connection(("127.0.0.1", self.up))
        if self.banner: c.sendall(self.banner)
        def c2u():
            try:
                while (d := c.recv(65536)): u.sendall(d)
            except OSError: pass
        def u2c():
            sent = 0
            try:
                while (d := u.recv(65536)):
                    if self.flip_after is not None and sent <= self.flip_after < sent + len(d):
                        b = bytearray(d); b[self.flip_after - sent] ^= 0x01; d = bytes(b); self.flip_after = None
                    sent += len(d); c.sendall(d)
            except OSError: pass
            try: c.shutdown(socket.SHUT_RDWR)
            except OSError: pass
        threading.Thread(target=c2u, daemon=True).start(); threading.Thread(target=u2c, daemon=True).start()

# ---- the shell: socket <-> wasm -------------------------------------------------------------------------------------------------------------------
class Client:
    def __init__(self, port, user=b"tester", password=b"s3cret", expect=None, cols=80, rows=24, chunking="whole"):
        self.sock = socket.create_connection(("127.0.0.1", port)); self.sock.settimeout(0.05)
        self.data = bytearray(); self.sent = bytearray(); self.chunking = chunking; self.eof = False
        nk.write(nk.nk_ssh_entropy_addr(), os.urandom(64)); nk.write(nk.nk_ssh_user_addr(), user); nk.write(nk.nk_ssh_pass_addr(), password)
        if expect: nk.write(nk.nk_ssh_expect_addr(), expect)
        nk.nk_ssh_begin(len(user), len(password), cols, rows, 1 if expect else 0)
        self.drain()
    def drain(self):
        n = nk.nk_out_len()
        if n:
            b = nk.read(OUT, n); nk.nk_out_clear(); self.sent += b
            try: self.sock.sendall(b)
            except OSError: pass
        dl = nk.nk_ssh_data_len()
        if dl:
            self.data += nk.read(nk.nk_ssh_data_addr(), dl); nk.nk_ssh_data_clear(); n2 = nk.nk_out_len()
            if n2: b = nk.read(OUT, n2); nk.nk_out_clear(); self.sent += b; self.sock.sendall(b)
        if nk.nk_ssh_txq_pending() > 0: nk.nk_ssh_pump(); self.drain_out()
    def drain_out(self):
        n = nk.nk_out_len()
        if n: b = nk.read(OUT, n); nk.nk_out_clear(); self.sent += b; self.sock.sendall(b)
    def step(self):
        try: d = self.sock.recv(65536)
        except socket.timeout: d = None
        except OSError: d = b""
        if d == b"":
            if not self.eof: nk.nk_ssh_transport_closed(); self.eof = True
        elif d:
            i = 0
            while i < len(d):
                n = len(d) - i if self.chunking == "whole" else (1 if self.chunking == "byte" else R.randint(1, 300))
                n = min(n, len(d) - i, nk.nk_in_space(), 60000)
                p = nk.nk_in_reserve(); nk.write(p, d[i:i + n]); nk.nk_ssh_feed(n); i += n; self.drain()
        self.drain()
    def run(self, until, seconds=20):
        t = time.time()
        while time.time() - t < seconds and not until(): self.step()
        return until()
    def phase(self): return nk.nk_ssh_phase()
    def type(self, b):
        b = bytes(b); off = 0
        while off < len(b):
            n = min(len(b) - off, 16000); nk.write(nk.nk_ssh_uin_addr(), b[off:off + n]); k = nk.nk_ssh_send(n); off += k; self.drain()
            if k == 0: self.step()
    def close(self):
        try: self.sock.close()
        except OSError: pass

def fp_b64(): return base64.b64encode(nk.read(nk.nk_ssh_fp_addr(), 32)).decode().rstrip("=")
def fp_raw_from(srv_fp): return base64.b64decode(srv_fp.split(":", 1)[1] + "==")

# ---- 1. the whole thing, every delivery mode ---------------------------------------------------------------------------------------------------------
for mode in ("whole", "byte", "random"):
    srv = Srv()
    c = Client(srv.port, chunking=mode)
    ok = c.run(lambda: c.phase() in (2, 8, 9), 15)
    check(ok and c.phase() == 2, f"[{mode}] key exchange completes (curve25519-sha256 + ed25519 host key + chacha20-poly1305) and stops to ask about the host key")
    check(fp_b64() == srv.fingerprint().split(":", 1)[1], f"[{mode}] the host key fingerprint the client computed equals asyncssh's own SHA256 fingerprint")
    check(nk.nk_ssh_seq_out() == 3, f"[{mode}] before the user accepts, the client has sent exactly KEXINIT, ECDH_INIT, NEWKEYS -- no credentials")
    check(srv.logins == 0, f"[{mode}] ...and the server saw no login attempt")
    check(nk.nk_ssh_accept_hostkey() == 1, f"[{mode}] the user accepts the fingerprint"); c.drain()
    ok = c.run(lambda: c.phase() in (5, 8, 9), 15)
    check(ok and c.phase() == 5, f"[{mode}] authentication (none -> password) succeeds and the shell opens (phase 5 running)")
    c.run(lambda: b"WELCOME" in c.data and b"\r\n" in c.data, 5)
    check(bytes(c.data).startswith(b"WELCOME TERM=xterm-256color SIZE=80x24\r\n"), f"[{mode}] pty-req arrived: TERM=xterm-256color, 80x24 -- got {bytes(c.data[:50])!r}")
    c.type(b"hello world\n"); c.run(lambda: b"got: hello world" in c.data, 5)
    check(b"got: hello world\r\n" in c.data, f"[{mode}] typed bytes reach the shell and its reply comes back decrypted")
    nk.nk_ssh_resize(132, 43); c.drain(); c.run(lambda: b"RESIZED 132x43" in c.data, 5)
    check(srv.resizes == [(132, 43)] and b"RESIZED 132x43" in c.data, f"[{mode}] window-change reaches the server")
    c.type(b"STDERR\n"); c.run(lambda: b"to-stderr" in c.data, 5); check(b"to-stderr" in c.data, f"[{mode}] extended (stderr) data is shown too")
    c.type(b"EXIT\n"); c.run(lambda: c.phase() in (8, 9), 5)
    check(c.phase() == 8 and nk.nk_err_kind() == 0, f"[{mode}] the server closing the channel is a clean end")
    check(nk.nk_ssh_exit_status() == 7, f"[{mode}] the remote exit status (7) is reported")
    c.close(); srv.stop()

# ---- 2. credentials ---------------------------------------------------------------------------------------------------------------------------------------
srv = Srv(); c = Client(srv.port, password=b"nope"); c.run(lambda: c.phase() == 2, 10); nk.nk_ssh_accept_hostkey(); c.drain()
c.run(lambda: c.phase() in (5, 8, 9), 10)
check(c.phase() == 9 and nk.nk_err_kind() == 4 and nk.nk_err_msg() == 5, "wrong password: AUTH_FAILED 'Wrong password'")
check(srv.failures >= 1 and srv.logins == 0, "...and the server really did reject it"); c.close(); srv.stop()

srv = Srv(kbdint=True); c = Client(srv.port); c.run(lambda: c.phase() == 2, 10); nk.nk_ssh_accept_hostkey(); c.drain()
c.run(lambda: c.phase() in (5, 8, 9), 10)
check(c.phase() == 5 and srv.logins == 1, "a server offering ONLY keyboard-interactive: the prompt is answered with the password and the login succeeds"); c.close(); srv.stop()
srv = Srv(kbdint=True); c = Client(srv.port, password=b"bad"); c.run(lambda: c.phase() == 2, 10); nk.nk_ssh_accept_hostkey(); c.drain()
c.run(lambda: c.phase() in (5, 8, 9), 10)
check(c.phase() == 9 and nk.nk_err_kind() == 4, "keyboard-interactive with a wrong password: AUTH_FAILED"); c.close(); srv.stop()

# ---- 3. host key pinning ---------------------------------------------------------------------------------------------------------------------------------------
srv = Srv(); expect = fp_raw_from(srv.fingerprint())
c = Client(srv.port, expect=expect); c.run(lambda: c.phase() in (5, 8, 9), 15)
check(c.phase() == 5 and nk.nk_ssh_trust() == 1, "a saved fingerprint that matches: straight through to a shell, no question asked"); c.close(); srv.stop()
srv = Srv(); wrong = bytes(R.randrange(256) for _ in range(32))
c = Client(srv.port, expect=wrong); c.run(lambda: c.phase() in (5, 8, 9), 15)
check(c.phase() == 9 and nk.nk_err_kind() == 7 and nk.nk_err_msg() == 16, "a saved fingerprint that does NOT match: HOST_KEY failure")
check(nk.nk_ssh_seq_out() == 3 and srv.logins == 0 and srv.failures == 0, "...and no credentials were sent (only KEXINIT, ECDH_INIT, NEWKEYS left the client; the server saw no auth attempt)")
check(nk.cstr(nk.nk_err_text_addr(7)) == "This device's identity changed since last time", "HOST_KEY has its plain-words sentence"); c.close(); srv.stop()
srv = Srv(); c = Client(srv.port); c.run(lambda: c.phase() == 2, 10); nk.nk_ssh_reject_hostkey(); c.drain()
check(c.phase() == 9 and nk.nk_err_kind() == 7 and srv.logins == 0 and srv.failures == 0, "the user declining the fingerprint fails the connection without sending anything"); c.close(); srv.stop()

# ---- 4. throughput and flow control (windows both ways) ----------------------------------------------------------------------------------------------------
srv = Srv(); c = Client(srv.port); c.run(lambda: c.phase() == 2, 10); nk.nk_ssh_accept_hostkey(); c.drain(); c.run(lambda: c.phase() == 5, 10)
c.run(lambda: b"WELCOME" in c.data, 5)
c.type(b"BIG\n"); t0 = time.time(); c.run(lambda: b"BIGDONE" in c.data, 30); dt = time.time() - t0
expect_big = "".join(chr(65 + (i % 26)) for i in range(300000)).encode()
check(expect_big in c.data and b"BIGDONE" in c.data, f"300 KB of server output arrives intact through the 64 KB window (flow control via nk-ssh-data-clear), {len(expect_big)/1e3/dt:.0f} KB/s")
# (asyncssh's pty applies a 1024-byte line limit to input -- even for its own client -- so the paste is made of lines)
paste = b"".join(bytes(R.choice(b"abcdefghij") for _ in range(499)) + b"\n" for _ in range(240))
srv.received.clear(); c.type(paste); c.run(lambda: len(srv.received) >= len(paste), 30)
check(bytes(srv.received) == paste, f"a {len(paste)//1000} KB paste is delivered to the server complete and in order (window-limited sending, queue, pump)")
c.close(); srv.stop()

# ---- 5. a hostile or odd network ---------------------------------------------------------------------------------------------------------------------------------
srv = Srv(); px = Proxy(srv.port, banner=b"Welcome to the lab network\r\nAuthorized users only\r\n")
c = Client(px.port); ok = c.run(lambda: c.phase() in (2, 8, 9), 10)
check(c.phase() == 2, "banner lines before the identification string are skipped"); c.close(); srv.stop()

# learn where the server's encrypted stream starts (identification + KEXINIT + ECDH reply + NEWKEYS are all plain), then flip one
# bit inside the first encrypted packet (SERVICE_ACCEPT) that follows
srv = Srv(); c = Client(srv.port); c.run(lambda: c.phase() == 2, 10); enc_start = nk.g(6); c.close(); srv.stop()
for where, label in ((enc_start + 6, "inside the encrypted body"), (enc_start + 1, "inside the encrypted LENGTH field"), (enc_start + 40, "inside the Poly1305 tag")):
    srv = Srv(); px = Proxy(srv.port, flip_after=where)
    c = Client(px.port); c.run(lambda: c.phase() in (2, 8, 9), 10); nk.nk_ssh_accept_hostkey(); c.drain(); c.run(lambda: c.phase() in (5, 8, 9), 10)
    check(c.phase() == 9 and nk.nk_err_kind() == 5 and nk.nk_err_msg() in (13, 14) and srv.logins == 0,
          f"one flipped bit {label} of the first encrypted server packet fails the connection (msg {nk.nk_err_msg()}), nothing proceeds"); c.close(); srv.stop()

srv = Srv(enc=("aes256-ctr",)); c = Client(srv.port); c.run(lambda: c.phase() in (2, 8, 9), 10)
check(c.phase() == 9 and nk.nk_err_msg() == 11, "a server with no chacha20-poly1305: refused with the plain message naming what is missing (no silent downgrade)")
check(nk.cstr(nk.nk_msg_text_addr(11)).startswith("The server doesn't offer the encryption"), "...in words"); c.close(); srv.stop()

# not an SSH server at all
ls = socket.socket(); ls.bind(("127.0.0.1", 0)); ls.listen(1); port = ls.getsockname()[1]
def http():
    s, _ = ls.accept(); s.sendall(b"HTTP/1.1 400 Bad Request\r\n\r\n" + b"x" * 3000); time.sleep(0.5); s.close()
threading.Thread(target=http, daemon=True).start()
c = Client(port); c.run(lambda: c.phase() in (8, 9), 5)
check(c.phase() == 9 and nk.nk_err_msg() == 12, "an HTTP server: 'That isn't an SSH server'"); c.close()

# the server just vanishes mid-handshake
ls2 = socket.socket(); ls2.bind(("127.0.0.1", 0)); ls2.listen(1); port2 = ls2.getsockname()[1]
def vanish():
    s, _ = ls2.accept(); s.sendall(b"SSH-2.0-test\r\n"); s.close()
threading.Thread(target=vanish, daemon=True).start()
c = Client(port2); c.run(lambda: c.phase() in (8, 9), 5)
check(c.phase() == 9, "the connection dropping mid-handshake is a failure, not a hang"); c.close()
done()
