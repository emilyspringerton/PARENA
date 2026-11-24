"""Shared test servers for the netkit tests: an asyncssh server on a background event loop, a TCP proxy for hostile-network
scenarios, and the libvncserver-based VNC test server (vncd.c). Test-only code."""
import asyncio, logging, os, socket, subprocess, threading, time
import asyncssh
logging.getLogger('asyncio').setLevel(logging.CRITICAL)   # asyncssh complaining about peers we drop on purpose

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



def start_vncd(vncd, libdir, port, password, rgb_out):
    """Start the real VNC server (see vncd.c). Returns the Popen; waits until it listens."""
    env = dict(os.environ)
    if libdir: env["LD_LIBRARY_PATH"] = libdir + ":" + env.get("LD_LIBRARY_PATH", "")
    p = subprocess.Popen([vncd, str(port), password, rgb_out], env=env, stderr=subprocess.PIPE, stdout=subprocess.DEVNULL)
    for _ in range(100):
        try: socket.create_connection(("127.0.0.1", port), 0.2).close(); break
        except OSError: time.sleep(0.05)
    time.sleep(0.2)
    return p
