"""The PARENA SSH client (nk_ssh*.prn, compiled to wasm) against the REAL OpenSSH sshd -- the server the router Pi runs.

An unprivileged sshd is started on a high port with its own host key and a private passwd/shadow (libnss_wrapper supplies the
user table, so no root and no system change). The Python below plays the Android shell: it owns the socket and moves bytes.

usage: test_ssh_openssh.py <netkit.wasm>     (skips cleanly if sshd or libnss_wrapper are not installed)
env:   SSHD=/usr/sbin/sshd   NSS_WRAPPER_LIB=<path to libnss_wrapper.so>
"""
import base64, glob, os, random, shutil, signal, socket, subprocess, sys, tempfile, time
sys.path.insert(0, os.path.dirname(__file__))

SSHD = os.environ.get("SSHD", "/usr/sbin/sshd")
cands = [os.environ.get("NSS_WRAPPER_LIB", "")] + glob.glob("/usr/lib/*/libnss_wrapper.so") + glob.glob(os.path.expanduser("~/.local/opt/nssw/root/usr/lib/*/libnss_wrapper.so"))
NSSW = next((c for c in cands if c and os.path.exists(c)), None)
if not os.path.exists(SSHD) or not NSSW or not shutil.which("ssh-keygen") or not shutil.which("openssl"):
    print("SKIP: needs OpenSSH sshd + ssh-keygen + openssl + libnss_wrapper (apt install openssh-server libnss-wrapper)"); sys.exit(0)

from nkharness import *
nk = NK(sys.argv[1]); R = random.Random(9)

# ---- start sshd ----------------------------------------------------------------------------------------------------------------------------------
d = tempfile.mkdtemp(prefix="nk_sshd_")
uid, gid = os.getuid(), os.getgid()
subprocess.check_call(["ssh-keygen", "-q", "-t", "ed25519", "-N", "", "-f", f"{d}/host"])
fp_line = subprocess.check_output(["ssh-keygen", "-lf", f"{d}/host.pub"]).decode().split()[1]       # SHA256:...
h = subprocess.check_output(["openssl", "passwd", "-6", "s3cret"]).decode().strip()
open(f"{d}/passwd", "w").write(f"tester:x:{uid}:{gid}:Tester:{d}:/bin/bash\nsshd:x:101:65534::/run/sshd:/usr/sbin/nologin\n")
open(f"{d}/group", "w").write(f"tester:x:{gid}:\nnogroup:x:65534:\n")
open(f"{d}/shadow", "w").write(f"tester:{h}:19000:0:99999:7:::\n")
s = socket.socket(); s.bind(("127.0.0.1", 0)); port = s.getsockname()[1]; s.close()
open(f"{d}/sshd_config", "w").write(f"""Port {port}
ListenAddress 127.0.0.1
HostKey {d}/host
PidFile {d}/sshd.pid
UsePAM no
PasswordAuthentication yes
KbdInteractiveAuthentication no
PubkeyAuthentication no
StrictModes no
PrintMotd no
LogLevel DEBUG1
""")
env = dict(os.environ, LD_PRELOAD=NSSW, NSS_WRAPPER_PASSWD=f"{d}/passwd", NSS_WRAPPER_GROUP=f"{d}/group", NSS_WRAPPER_SHADOW=f"{d}/shadow")
srv = subprocess.Popen([SSHD, "-D", "-e", "-f", f"{d}/sshd_config"], env=env, stdout=subprocess.DEVNULL, stderr=open(f"{d}/sshd.log", "w"))
for _ in range(100):
    try: socket.create_connection(("127.0.0.1", port), 0.2).close(); break
    except OSError: time.sleep(0.05)
def cleanup():
    srv.send_signal(signal.SIGTERM)
    try: srv.wait(5)
    except subprocess.TimeoutExpired: srv.kill()
    shutil.rmtree(d, ignore_errors=True)
import atexit; atexit.register(cleanup)
ver = subprocess.run([SSHD, "-V"], capture_output=True, text=True).stderr.split(",")[0]
print("      server under test:", ver)

# ---- the shell (socket <-> wasm), as in test_ssh_interop.py -------------------------------------------------------------------------------------------
class Client:
    def __init__(self, user=b"tester", password=b"s3cret", expect=None, cols=80, rows=24):
        self.sock = socket.create_connection(("127.0.0.1", port)); self.sock.settimeout(0.05); self.data = bytearray(); self.eof = False
        nk.write(nk.nk_ssh_entropy_addr(), os.urandom(64)); nk.write(nk.nk_ssh_user_addr(), user); nk.write(nk.nk_ssh_pass_addr(), password)
        if expect: nk.write(nk.nk_ssh_expect_addr(), expect)
        nk.nk_ssh_begin(len(user), len(password), cols, rows, 1 if expect else 0); self.drain()
    def drain(self):
        n = nk.nk_out_len()
        if n: b = nk.read(OUT, n); nk.nk_out_clear(); self.sock.sendall(b)
        dl = nk.nk_ssh_data_len()
        if dl:
            self.data += nk.read(nk.nk_ssh_data_addr(), dl); nk.nk_ssh_data_clear(); n2 = nk.nk_out_len()
            if n2: b = nk.read(OUT, n2); nk.nk_out_clear(); self.sock.sendall(b)
        if nk.nk_ssh_txq_pending() > 0:
            nk.nk_ssh_pump(); n3 = nk.nk_out_len()
            if n3: b = nk.read(OUT, n3); nk.nk_out_clear(); self.sock.sendall(b)
    def step(self):
        try: dd = self.sock.recv(65536)
        except socket.timeout: dd = None
        except OSError: dd = b""
        if dd == b"":
            if not self.eof: nk.nk_ssh_transport_closed(); self.eof = True
        elif dd:
            i = 0
            while i < len(dd):
                n = min(len(dd) - i, nk.nk_in_space(), 60000); p = nk.nk_in_reserve(); nk.write(p, dd[i:i + n]); nk.nk_ssh_feed(n); i += n; self.drain()
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
    def text(self): return bytes(self.data).decode("utf-8", "replace")

# ---- 1. the whole login ------------------------------------------------------------------------------------------------------------------------------------
c = Client()
ok = c.run(lambda: c.phase() in (2, 8, 9), 15)
check(ok and c.phase() == 2, f"key exchange with real OpenSSH completes and stops to ask about the host key (state {nk.nk_ssh_state()}, err {nk.nk_err_kind()}/{nk.nk_err_msg()})")
mine = "SHA256:" + base64.b64encode(nk.read(nk.nk_ssh_fp_addr(), 32)).decode().rstrip("=")
check(mine == fp_line, f"the fingerprint the client computed equals `ssh-keygen -l` ({mine})")
check(nk.nk_ssh_sigok() == 1, "OpenSSH's Ed25519 signature over the exchange hash verifies against its host key")
check(nk.nk_ssh_seq_out() == 3, "nothing but KEXINIT, ECDH_INIT, NEWKEYS has been sent before the user accepts")
nk.nk_ssh_accept_hostkey(); c.drain()
ok = c.run(lambda: c.phase() in (5, 8, 9), 20)
check(ok and c.phase() == 5, f"password authentication succeeds against real OpenSSH and the shell opens (state {nk.nk_ssh_state()}, err {nk.nk_err_kind()}/{nk.nk_err_msg()})")
c.run(lambda: len(c.data) > 0, 5); time.sleep(0.3); c.run(lambda: False, 0.5)
c.type(b"PS1='RDY> '; export PS1\n"); c.run(lambda: c.text().count("RDY>") >= 1, 8)
c.type(b"echo $((6*7)) $TERM\n"); ok = c.run(lambda: "42 xterm-256color" in c.text(), 8)
check(ok, "the shell runs commands: arithmetic and $TERM (xterm-256color) come back")
c.type(b"stty size\n"); ok = c.run(lambda: "24 80" in c.text(), 8)
check(ok, "the pty is 24x80 as requested (stty size)")
nk.nk_ssh_resize(132, 43); c.drain(); time.sleep(0.2); c.type(b"stty size\n"); ok = c.run(lambda: "43 132" in c.text(), 8)
check(ok, "window-change reaches OpenSSH: stty size reports 43 132 after nk-ssh-resize")
n0 = len(c.data); c.type(b"head -c 300000 /dev/zero | tr '\\0' x; echo; echo DONE$((1+2))\n")
ok = c.run(lambda: "DONE3" in c.text()[n0:], 30)
txt = c.text()[n0:]
check(ok and txt.count("x") >= 300000, f"300 KB of shell output arrives complete through the windows ({txt.count('x')} x's)")
paste = b"echo " + b"A" * 3000 + b" | wc -c\n"
c.type(paste); ok = c.run(lambda: "3001" in c.text()[n0 + len(txt) - 50:], 10)
check(ok, "a 3000-character command line (typed bytes through the queue) is delivered intact")
c.type(b"exit 3\n"); c.run(lambda: c.phase() in (8, 9), 10)
check(c.phase() == 8 and nk.nk_err_kind() == 0 and nk.nk_ssh_exit_status() == 3, f"`exit 3` ends the session cleanly with exit status 3 (phase {c.phase()}, status {nk.nk_ssh_exit_status()})")
c.close()

# ---- 2. credentials and pinning against OpenSSH ------------------------------------------------------------------------------------------------------------------
c = Client(password=b"wrong"); c.run(lambda: c.phase() == 2, 10); nk.nk_ssh_accept_hostkey(); c.drain(); c.run(lambda: c.phase() in (5, 8, 9), 30)
check(c.phase() == 9 and nk.nk_err_kind() == 4, f"a wrong password is AUTH_FAILED against real OpenSSH (kind {nk.nk_err_kind()} msg {nk.nk_err_msg()})"); c.close()
expect = base64.b64decode(fp_line.split(":", 1)[1] + "==")
c = Client(expect=expect); c.run(lambda: c.phase() in (5, 8, 9), 20)
check(c.phase() == 5 and nk.nk_ssh_trust() == 1, "a pinned fingerprint that matches goes straight to a shell"); c.type(b"exit\n"); c.run(lambda: c.phase() in (8, 9), 8); c.close()
c = Client(expect=bytes(32)); c.run(lambda: c.phase() in (5, 8, 9), 15)
check(c.phase() == 9 and nk.nk_err_kind() == 7 and nk.nk_ssh_seq_out() == 3, "a pinned fingerprint that differs fails with HOST_KEY and sends no credentials"); c.close()
done()
