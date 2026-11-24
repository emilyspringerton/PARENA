"""End-to-end: the GENERATED page (nk-host.js + the three wasm modules + the real index.html) run in Node, talking through a
socket-backed NetkitNative stub to a REAL asyncssh server and a REAL VNC server (libvncserver). See host_e2e.mjs for what is stubbed
(a DOM, and the Android shell) and what is not (everything else).

usage: test_host_e2e.py <web-dir> <index.html> <vncd binary> [libdir]       (web-dir from scripts/build_netkit_web.sh)
"""
import base64, json, os, shutil, subprocess, sys, tempfile, threading, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from nkservers import Srv, start_vncd

web, html = sys.argv[1], sys.argv[2]
vncd = sys.argv[3] if len(sys.argv) > 3 else ""
libdir = sys.argv[4] if len(sys.argv) > 4 else ""
node = shutil.which("node")
if not node: print("SKIP: node not found"); sys.exit(0)
if not vncd or not os.path.exists(vncd): print("SKIP: no vncd test server (see test_rfb_interop.py)"); sys.exit(0)

here = os.path.dirname(os.path.abspath(__file__))
tmp = tempfile.mkdtemp(prefix="nk_e2e_")
srv = Srv()
fp = srv.fingerprint()
vport = 5990 + (os.getpid() % 400) * 2
rgb = os.path.join(tmp, "final.rgb")
cfg = {"ssh": {"port": srv.port, "fp": fp, "fpB64": base64.b64encode(base64.b64decode(fp.split(":", 1)[1] + "==")).decode()},
       "vnc": {"port": vport, "rgb": rgb}}

# The node side reads the SSH server's login counters from two files this process keeps current (node cannot call into Python).
stop = threading.Event()
def pump():
    while not stop.is_set():
        for name, v in (("logins", srv.logins), ("failures", srv.failures)):
            with open(os.path.join(tmp, name + ".tmp"), "w") as f: f.write(str(v))
            os.replace(os.path.join(tmp, name + ".tmp"), os.path.join(tmp, name))
        time.sleep(0.02)
for n in ("logins", "failures"): open(os.path.join(tmp, n), "w").write("0")
threading.Thread(target=pump, daemon=True).start()
src = open(os.path.join(here, "host_e2e.mjs")).read()
marker = "const web = process.argv[2], htmlPath = process.argv[3], cfg = JSON.parse(process.argv[4] || '{}');"
assert marker in src
src = src.replace(marker, marker + f"\ncfg.ssh.loginsNow = () => Number(fs.readFileSync('{tmp}/logins', 'utf8') || 0);\ncfg.ssh.failuresNow = () => Number(fs.readFileSync('{tmp}/failures', 'utf8') || 0);")
runner = os.path.join(tmp, "run.mjs"); open(runner, "w").write(src)

vp = start_vncd(vncd, libdir, vport, "secret", rgb)       # its timeline starts when the page connects
code = 1
try:
    r = subprocess.run([node, "--no-warnings", runner, web, html, json.dumps(cfg)], capture_output=True, text=True, timeout=240)
    print(r.stdout + r.stderr)
    ok = (srv.logins == 2 and srv.failures == 1)
    print(("PASS" if ok else "FAIL") + f": the server's own counters agree: {srv.logins} successful logins (the first one and the one from Saved), {srv.failures} rejected password (the deliberately wrong one); the changed-host-key attempt sent none")
    code = r.returncode or (0 if ok else 1)
finally:
    stop.set(); vp.kill(); srv.stop(); shutil.rmtree(tmp, ignore_errors=True)
sys.exit(code)
