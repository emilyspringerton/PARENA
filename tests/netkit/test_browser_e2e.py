"""The page in a REAL browser engine (headless Chromium via Playwright): the actual index.html + the generated nk-host.js + the wasm
modules, served same-origin WITH the Content-Security-Policy the Android shell sets (so a CSP violation or an inline-script slip fails
here), driven with real clicks and real key presses against a real asyncssh server and a real VNC server. NetkitNative is an in-page
shim over a WebSocket-to-TCP bridge (browser_bridge.py). Screenshots are written for a human to look at.

What this adds over host_e2e.mjs (the same page in Node against a DOM stub): real DOM, real CSS layout, real canvas, real wasm in the
engine the Android WebView is built on, real keyboard events -- and a pixel-exact check that what the browser paints into the terminal
canvas equals an independent re-render of the same bytes by a separately instantiated terminal module.

usage: test_browser_e2e.py <web-dir> <html-dir> <vncd> [libdir [screenshot-dir]]
"""
import base64, ctypes, os, socket, sys, tempfile, threading, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from nkharness import check, done
from nkservers import Srv, start_vncd
from browser_bridge import Site

web_dir, html_dir, vncd = sys.argv[1], sys.argv[2], sys.argv[3]
libdir = sys.argv[4] if len(sys.argv) > 4 else ""
shots = sys.argv[5] if len(sys.argv) > 5 else tempfile.mkdtemp(prefix="nk_shots_")
os.makedirs(shots, exist_ok=True)
try:
    from playwright.sync_api import sync_playwright
except ImportError:
    print("SKIP: playwright not installed"); sys.exit(0)
extra = os.path.expanduser("~/.local/opt/chromelibs/root/usr/lib/x86_64-linux-gnu")
env = dict(os.environ)
if os.path.isdir(extra): env["LD_LIBRARY_PATH"] = extra + ":" + env.get("LD_LIBRARY_PATH", "")

# ---- servers -------------------------------------------------------------------------------------------------------------------------------------
srv = Srv(); fp = srv.fingerprint()
vport = 6200 + (os.getpid() % 300) * 2
rgb = os.path.join(tempfile.mkdtemp(prefix="nk_br_"), "final.rgb")
vp = start_vncd(vncd, libdir, vport, "secret", rgb)
def listener(host, port):
    s = socket.socket(); s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    try: s.bind((host, port)); s.listen(8)
    except OSError: return None
    threading.Thread(target=lambda: [c.close() for c in iter(lambda: s.accept()[0], None)], daemon=True).start(); return s
ls = [listener("127.0.0.3", 5900), listener("127.0.0.4", 5901)]
site = Site(html_dir, web_dir)

# ---- an independent terminal to compare pixels with --------------------------------------------------------------------------------------------------
from wasmtime import Store, Module, Instance
class RefTerm:
    def __init__(self, path):
        self.s = Store(); m = Module.from_file(self.s.engine, path); self.ex = Instance(self.s, m, []).exports(self.s); self.ex["term_init"](self.s)
        self.mem = self.ex["memory"]
    def base(self): return ctypes.addressof(self.mem.data_ptr(self.s).contents)
    def feed(self, b): ctypes.memmove(self.base() + 0x20000, b, len(b)); self.ex["term_feed"](self.s, len(b))
    def frame(self): self.ex["term_set_cursor"](self.s, 0); self.ex["term_render"](self.s); return ctypes.string_at(self.base() + 0x300000, 800 * 544 * 4)

GRAB = """(id) => { const c = document.getElementById(id); const d = c.getContext('2d').getImageData(0, 0, c.width, c.height).data;
  let s = ''; for (let i = 0; i < d.length; i += 8192) s += String.fromCharCode.apply(null, d.subarray(i, i + 8192)); return [c.width, c.height, btoa(s)]; }"""
def grab(page, id):
    w, h, b = page.evaluate(GRAB, id); return w, h, base64.b64decode(b)

def wait(pred, timeout=20.0, what=""):
    """Poll from Python (Playwright's wait_for_function evaluates a string, which the page's strict CSP rightly forbids)."""
    end = time.time() + timeout
    while time.time() < end:
        try:
            if pred(): return True
        except Exception: pass
        time.sleep(0.1)
    raise AssertionError("timed out waiting for " + what)

errors = []
with sync_playwright() as p:
    try:
        browser = p.chromium.launch(env=env)
    except Exception as e:
        print("SKIP: could not launch Chromium (run `python3 -m playwright install --with-deps chromium`):", str(e).splitlines()[0]); sys.exit(0)
    ctx = browser.new_context(viewport={"width": 1280, "height": 800}, device_scale_factor=1)
    page = ctx.new_page()
    # (Chrome's "willReadFrequently" hint is about THIS test's own getImageData pixel reads, not the page)
    page.on("console", lambda m: errors.append(f"console.{m.type}: {m.text}") if m.type in ("error", "warning") and "willReadFrequently" not in m.text else None)
    page.on("pageerror", lambda e: errors.append(f"pageerror: {e}"))
    page.on("requestfailed", lambda r: errors.append(f"requestfailed: {r.url} {r.failure}"))
    page.goto(f"http://127.0.0.1:{site.port}/")

    # ---- boot + scan ---------------------------------------------------------------------------------------------------------------------------------------
    wait(lambda: page.inner_text('#scan-status').startswith('Scanned'), 25, 'the scan to finish')
    check(page.inner_text("#net-pill") == "127.0.0.0/29", f"boots in a real browser; the network pill shows the range ({page.inner_text('#net-pill')})")
    n = page.locator("#device-list li.dev").count()
    check(n >= 5, f"the scan lists {n} devices as real DOM cards")
    check(page.locator('li[data-ip="127.0.0.3"] .chip', has_text="VNC 5900").count() == 1 and page.locator('li[data-ip="127.0.0.4"] .chip', has_text="VNC 5901").count() == 1, "the open ports show as chips on the right devices")
    wait(lambda: page.inner_text('li[data-ip="127.0.0.4"] .dev-name') == 'imac.lan', 5, 'the resolved name')
    check(True, "a resolved name replaces the address as the card title")
    page.screenshot(path=os.path.join(shots, "01-devices-tablet.png"))
    page.click('li[data-ip="127.0.0.4"] [data-act=pin]')
    check(page.locator("#device-list li.dev").first.get_attribute("data-ip") == "127.0.0.4" and "pinned" in page.locator("#device-list li.dev").first.get_attribute("class"), "a real click on the star pins a device and it sorts first")
    # touch targets: every button in the list is at least 40px high (a tablet, a finger)
    hs = page.evaluate("() => [...document.querySelectorAll('#device-list button, #tabs button, #btn-scan')].filter(b => b.offsetParent !== null).map(b => b.getBoundingClientRect().height)")
    check(min(hs) >= 40, f"every tappable control is at least 40px tall (smallest {min(hs):.0f}px)")

    # ---- SSH -------------------------------------------------------------------------------------------------------------------------------------------------
    page.click("#manual summary"); page.fill("#m-host", "127.0.0.1"); page.fill("#m-port", str(srv.port)); page.click("#btn-m-connect")
    check(page.is_visible("#dlg-connect"), "Connect opens the dialog in the real layout")
    page.fill("#c-user", "tester"); page.fill("#c-pass", "s3cret"); page.click("#c-go")
    page.wait_for_selector("#dlg-hostkey", state="visible", timeout=20000)
    check(page.inner_text("#hk-fp") == fp, f"the trust dialog shows the server's real fingerprint ({fp})")
    page.screenshot(path=os.path.join(shots, "02-trust-dialog.png"))
    page.click("#hk-trust")
    page.wait_for_selector("#view-ssh", state="visible", timeout=20000)
    time.sleep(1.0)
    page.keyboard.type("hello world", delay=8); page.keyboard.press("Enter")
    time.sleep(1.5)
    # pixel-exact: what the browser painted == an independent terminal fed the same bytes (cursor hidden in both)
    ref = RefTerm(os.path.join(web_dir, "nkterm.wasm"))
    ref.feed(b"WELCOME TERM=xterm-256color SIZE=100x34\r\nhello world\r\ngot: hello world\r\n")   # the pty echoes what was typed
    want = ref.frame()
    got = None
    for _ in range(10):                                  # the page blinks the cursor; take a frame with it off
        got = grab(page, "term-canvas")[2]
        # blank the cursor cell (row 2, col 0) in both before comparing
        def blank(buf):
            b = bytearray(buf)
            for y in range(32, 48):
                for x in range(0, 8): b[(y * 800 + x) * 4:(y * 800 + x) * 4 + 4] = b"\x00\x00\x00\x00"
            return bytes(b)
        if blank(got) == blank(want): break
        time.sleep(0.2)
    check(blank(got) == blank(want), "the terminal canvas in the browser is PIXEL-IDENTICAL to an independent re-render of the same server output (800x544 RGBA)")
    page.screenshot(path=os.path.join(shots, "03-terminal-tablet.png"))
    page.click('#keybar [data-key=ctrl]'); check("on" in page.get_attribute("#key-ctrl", "class"), "tapping Ctrl highlights it (real CSS class)")
    page.click('#keybar [data-key=ctrl]') if False else None
    page.keyboard.press("c")
    check("on" not in (page.get_attribute("#key-ctrl", "class") or ""), "the sticky Ctrl is used up by the next real key press")
    page.set_viewport_size({"width": 412, "height": 915}); time.sleep(0.5)
    page.screenshot(path=os.path.join(shots, "04-terminal-phone.png"))
    cw = page.evaluate("() => document.getElementById('term-canvas').getBoundingClientRect().width")
    check(cw <= 412, f"on a phone-width screen the terminal scales to fit ({cw:.0f}px wide)")
    sw = page.evaluate("() => document.documentElement.scrollWidth")
    check(sw <= 412, f"...with no horizontal scrolling of the page (scrollWidth {sw})")
    page.set_viewport_size({"width": 1280, "height": 800})
    page.keyboard.type("EXIT"); page.keyboard.press("Enter")
    wait(lambda: 'Session ended' in page.inner_text('#banner-text'), 10, 'the session-ended banner')
    check("exit status 7" in page.inner_text("#banner-text"), f"the banner reports the session end ({page.inner_text('#banner-text')})")
    page.click("#btn-ssh-close"); page.click("#banner-close")
    check(page.is_visible("#view-devices") and not page.is_visible("#view-ssh"), "Disconnect returns to the device list")

    # ---- VNC ---------------------------------------------------------------------------------------------------------------------------------------------------
    page.select_option("#m-proto", "vnc"); page.fill("#m-host", "127.0.0.1"); page.fill("#m-port", str(vport)); page.click("#btn-m-connect")
    check(not page.is_visible("#c-user-row"), "the Screen dialog has no user field")
    page.fill("#c-pass", "secret"); page.click("#c-go")
    page.wait_for_selector("#view-vnc", state="visible", timeout=20000)
    time.sleep(0.8)
    page.screenshot(path=os.path.join(shots, "05-vnc-live.png"))
    wait(lambda: 'closed the connection' in page.inner_text('#banner-text'), 25, 'the VNC server to hang up')
    time.sleep(0.3)
    w, h, pix = grab(page, "vnc-canvas")
    check((w, h) == (256, 160), f"the Screen canvas followed the remote resize to {w}x{h}")
    raw = open(rgb, "rb").read(); body = raw.split(b"\n", 1)[1]
    diff = sum(1 for i in range(256 * 160) if bytes(pix[i * 4:i * 4 + 3]) != body[i * 3:i * 3 + 3] or pix[i * 4 + 3] != 255)
    check(diff == 0, f"the image painted by the browser equals the VNC server's own final framebuffer ({diff} differing pixels of {256 * 160})")
    page.screenshot(path=os.path.join(shots, "06-vnc-final.png"))
    page.click("#btn-fit"); page.click("#btn-vnc-close")
    check(page.is_visible("#view-devices"), "Close returns to the device list")

    # ---- the page as a whole ---------------------------------------------------------------------------------------------------------------------------------
    inline = page.evaluate("() => [...document.scripts].filter(s => !s.src && s.textContent.trim()).length")
    check(inline == 0, "no inline scripts on the page")
    check(not errors, "no console errors, page errors, failed requests or CSP violations" + ("" if not errors else " -- " + "; ".join(errors[:5])))
    browser.close()
print("      screenshots:", shots)
for s in ls:
    if s: s.close()
vp.kill(); srv.stop()
done()
