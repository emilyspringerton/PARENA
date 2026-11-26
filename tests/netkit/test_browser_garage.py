"""The garage, the arcade and SURVIVAL in a REAL browser engine (headless Chromium via Playwright), under the same Content-Security-Policy the Android
shell sets (so a CSP violation, a blocked fetch or an inline-script slip fails here): the actual index.html + the generated nk-host.js +
the wasm modules + BRAWLPIT's wasm and art, served same-origin. No SSH or VNC server is needed. Real clicks, real touch (multi-touch through
the DevTools protocol), real canvas pixels; screenshots are written for a human to look at.

usage: test_browser_garage.py <web-dir> <html-dir> [screenshot-dir]
"""
import os, sys, tempfile, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
web_dir, html_dir = sys.argv[1], sys.argv[2]
shots = sys.argv[3] if len(sys.argv) > 3 else tempfile.mkdtemp(prefix="nk_garage_shots_")
os.makedirs(shots, exist_ok=True)
try:
    from playwright.sync_api import sync_playwright
except ImportError:
    print("SKIP: playwright not installed"); sys.exit(0)
from nkharness import check, done
from browser_bridge import Site
extra = os.path.expanduser("~/.local/opt/chromelibs/root/usr/lib/x86_64-linux-gnu")
env = dict(os.environ)
if os.path.isdir(extra): env["LD_LIBRARY_PATH"] = extra + ":" + env.get("LD_LIBRARY_PATH", "")
site = Site(html_dir, web_dir)

def wait(pred, timeout=20.0, what=""):
    end = time.time() + timeout
    while time.time() < end:
        try:
            if pred(): return True
        except Exception: pass
        time.sleep(0.1)
    raise AssertionError("timed out waiting for " + what)

# a fingerprint of the arcade canvas: how many of a grid of samples are lit, and a hash of them (changes when the game draws something new)
SAMPLE = """() => { const c = document.getElementById('bp-canvas'); const d = c.getContext('2d').getImageData(0, 0, c.width, c.height).data;
  let lit = 0, h = 0; for (let i = 0; i < d.length; i += 4 * 211) { if (d[i] | d[i+1] | d[i+2]) lit++; h = (h * 31 + d[i] + d[i+1] * 7 + d[i+2] * 13) >>> 0; } return [lit, h]; }"""

errors = []
with sync_playwright() as p:
    try:
        browser = p.chromium.launch(env=env)
    except Exception as e:
        print("SKIP: could not launch Chromium:", str(e).splitlines()[0]); sys.exit(0)
    ctx = browser.new_context(viewport={"width": 1280, "height": 800}, device_scale_factor=1, has_touch=True)
    page = ctx.new_page()
    page.on("console", lambda m: errors.append(f"console.{m.type}: {m.text}") if m.type in ("error", "warning") and "willReadFrequently" not in m.text else None)
    page.on("pageerror", lambda e: errors.append(f"pageerror: {e}"))
    page.on("requestfailed", lambda r: errors.append(f"requestfailed: {r.url} {r.failure}"))
    page.goto(f"http://127.0.0.1:{site.port}/")

    page.wait_for_selector("#obj-5", state="visible", timeout=20000)
    check(page.is_visible("#view-garage") and not page.is_visible("#view-devices"), "the page opens on the garage")
    check(all(page.is_visible(f"#obj-{i}") for i in (1, 2, 3, 4, 5)), "five objects are visible on the floor (the fifth is SURVIVAL)")
    check(page.inner_text("#brand").replace("\n", "") == "SHANKPHONE", "the app is the SHANKPHONE")
    boxes = [page.locator(f"#obj-{i}").bounding_box() for i in (1, 2, 3, 4, 5)]
    check(all(b["width"] >= 44 and b["height"] >= 44 for b in boxes), f"every object is a finger-sized target (smallest {min(min(b['width'], b['height']) for b in boxes):.0f}px)")
    check(all(boxes[i]["x"] + boxes[i]["width"] <= boxes[i + 1]["x"] + 1 for i in range(4)), "landscape: the five objects stand in a row without overlapping")
    check(page.locator("#lamp-2").get_attribute("class") == "lamp l0" and page.inner_text("#status-2") == "No session", "the terminal's lamp is off: there is no session")
    page.screenshot(path=os.path.join(shots, "10-garage-landscape.png"))
    # portrait: the same wasm layout reflows to two columns
    page.set_viewport_size({"width": 800, "height": 1280}); time.sleep(0.8)
    pb = [page.locator(f"#obj-{i}").bounding_box() for i in (1, 2, 3, 4)]
    check(abs(pb[0]["y"] - pb[1]["y"]) < 2 and pb[2]["y"] > pb[0]["y"] + pb[0]["height"] - 1, "portrait: the garage reflows to two columns (decided by the wasm layout)")
    page.screenshot(path=os.path.join(shots, "11-garage-portrait.png"))
    page.set_viewport_size({"width": 1280, "height": 800}); time.sleep(0.8)

    # the terminal object with no session -> device list with a hint
    page.click("#obj-2")
    check(page.is_visible("#view-devices") and "SSH" in page.inner_text("#devices-hint"), "tapping the terminal with no session opens the device list with a hint")
    page.click("#tab-garage")

    # ---- the arcade -------------------------------------------------------------------------------------------------------------------------------------------
    page.click("#obj-4")
    check(page.is_visible("#view-arcade") and page.is_visible("#bp-canvas"), "tapping the arcade cabinet opens the arcade view with BRAWLPIT's canvas")
    wait(lambda: page.locator("#arcade-msg").is_hidden(), 30, "BRAWLPIT to load")
    time.sleep(1.0)
    lit, h1 = page.evaluate(SAMPLE)
    check(lit > 50, f"BRAWLPIT's picture is on the canvas ({lit} lit samples): fetched from the app's own origin, instantiated with no imports, under the app CSP")
    page.screenshot(path=os.path.join(shots, "12-arcade.png"))
    box = page.locator("#bp-canvas").bounding_box()
    check(abs(box["width"] / box["height"] - 16 / 9) < 0.02, f"the canvas keeps BRAWLPIT's 16:9 shape ({box['width']:.0f}x{box['height']:.0f})")
    # touch: two fingers at once (CDP multi-touch) on Right and Jump; the picture must move on and the buttons light
    def btn_center(b): r = page.locator(f'#arc-pad [data-bp="{b}"]').bounding_box(); return {"x": r["x"] + r["width"] / 2, "y": r["y"] + r["height"] / 2}
    cdp = ctx.new_cdp_session(page)
    pts = [dict(btn_center(2), id=1), dict(btn_center(5), id=2)]
    cdp.send("Input.dispatchTouchEvent", {"type": "touchStart", "touchPoints": pts})
    time.sleep(0.4)
    check(page.evaluate("() => document.querySelectorAll('#arc-pad .down').length") == 2, "two fingers hold two buttons at once (multi-touch): both show pressed")
    cdp.send("Input.dispatchTouchEvent", {"type": "touchEnd", "touchPoints": []})
    time.sleep(0.3)
    check(page.evaluate("() => document.querySelectorAll('#arc-pad .down').length") == 0, "lifting the fingers releases both buttons")
    # menus by touch: OK (Enter) advances the game's own screen, so the picture changes
    for _ in range(3):
        page.click('#arc-pad [data-bp="10"]'); time.sleep(0.6)
    lit2, h2 = page.evaluate(SAMPLE)
    check(h2 != h1, "pressing OK on the touch pad changes what BRAWLPIT shows (the key reached the wasm game)")
    page.screenshot(path=os.path.join(shots, "13-arcade-after-ok.png"))
    # back to the garage; the loop stops
    page.click("#btn-arcade-exit")
    check(page.is_visible("#view-garage") and not page.is_visible("#view-arcade"), "Back to garage returns to the garage")
    wait(lambda: page.inner_text("#status-4") == "Ready" and page.locator("#lamp-4").get_attribute("class") == "lamp l1", 5, "the cabinet's lamp")
    check(True, "the cabinet's lamp is green and says Ready")
    _, ha = page.evaluate(SAMPLE); time.sleep(0.5); _, hb = page.evaluate(SAMPLE)
    check(ha == hb, "BRAWLPIT stops drawing while the arcade is off screen")

    # ---- SURVIVAL: the SHANKPHONE's survival button --------------------------------------------------------------------------------------------------
    SV = SAMPLE.replace("bp-canvas", "sv-canvas")
    page.click("#obj-5")
    check(page.is_visible("#view-survival") and page.is_visible("#sv-canvas"), "tapping SURVIVAL opens the survival view")
    wait(lambda: page.locator("#survival-msg").is_hidden(), 30, "SURVIVAL to load")
    time.sleep(1.0)
    lit, s1 = page.evaluate(SV)
    check(lit > 50, f"SURVIVAL's title is on the canvas ({lit} lit samples): survival.wasm fetched from the app's origin, no imports, under the app CSP")
    page.screenshot(path=os.path.join(shots, "20-survival-title.png"))
    box = page.locator("#sv-canvas").bounding_box()
    check(abs(box["width"] / box["height"] - 16 / 9) < 0.02, f"the canvas keeps the 16:9 shape ({box['width']:.0f}x{box['height']:.0f})")
    # tap PLAY: the engine's button at framebuffer (250..390, 188..220) -> CSS pixels
    page.mouse.click(box["x"] + box["width"] * 320 / 640, box["y"] + box["height"] * 204 / 360)
    time.sleep(3.2)   # the GET READY countdown
    _, s2 = page.evaluate(SV)
    check(s2 != s1, "tapping PLAY on the picture starts a run (the picture changes)")
    page.screenshot(path=os.path.join(shots, "21-survival-play.png"))
    # the stick: one finger pushed up for a second walks the hero forward; a second finger drags the picture to look
    st = page.locator("#sv-stick").bounding_box(); cx, cy = st["x"] + st["width"] / 2, st["y"] + st["height"] / 2
    cdp2 = ctx.new_cdp_session(page)
    cdp2.send("Input.dispatchTouchEvent", {"type": "touchStart", "touchPoints": [{"x": cx, "y": cy, "id": 1}]})
    cdp2.send("Input.dispatchTouchEvent", {"type": "touchMove", "touchPoints": [{"x": cx, "y": cy - 60, "id": 1}]})
    time.sleep(1.0)
    _, s3 = page.evaluate(SV)
    knob = page.evaluate("() => getComputedStyle(document.getElementById('sv-knob')).transform")
    check(knob not in ("none", "") and s3 != s2, f"pushing the stick moves the knob and the view ({knob})")
    mx, my = box["x"] + box["width"] * 0.55, box["y"] + box["height"] * 0.4
    cdp2.send("Input.dispatchTouchEvent", {"type": "touchMove", "touchPoints": [{"x": cx, "y": cy - 60, "id": 1}, {"x": mx, "y": my, "id": 2}]})
    for k in range(1, 8):
        cdp2.send("Input.dispatchTouchEvent", {"type": "touchMove", "touchPoints": [{"x": cx, "y": cy - 60, "id": 1}, {"x": mx + 20 * k, "y": my, "id": 2}]})
        time.sleep(0.05)
    time.sleep(0.3)
    page.screenshot(path=os.path.join(shots, "22-survival-moved-and-looked.png"))
    cdp2.send("Input.dispatchTouchEvent", {"type": "touchEnd", "touchPoints": []})
    time.sleep(0.3)
    knob2 = page.evaluate("() => getComputedStyle(document.getElementById('sv-knob')).transform")
    check(knob2 in ("none", "matrix(1, 0, 0, 1, 0, 0)"), f"lifting the finger recentres the stick ({knob2})")
    # the buttons: Fire held shows pressed and releases
    fb_ = page.locator('#sv-pad [data-sv="6"]').bounding_box()
    cdp2.send("Input.dispatchTouchEvent", {"type": "touchStart", "touchPoints": [{"x": fb_["x"] + 20, "y": fb_["y"] + 20, "id": 3}]})
    time.sleep(0.3)
    check(page.evaluate("() => document.querySelectorAll('#sv-pad .down').length") == 1, "holding Fire shows it pressed")
    cdp2.send("Input.dispatchTouchEvent", {"type": "touchEnd", "touchPoints": []})
    time.sleep(0.2)
    check(page.evaluate("() => document.querySelectorAll('#sv-pad .down').length") == 0, "lifting releases it")
    page.click("#btn-survival-exit")
    check(page.is_visible("#view-garage") and not page.is_visible("#view-survival"), "Back to garage leaves SURVIVAL")
    wait(lambda: page.inner_text("#status-5") == "Ready" and page.locator("#lamp-5").get_attribute("class") == "lamp l1", 5, "the survival lamp")
    check(True, "the survival lamp is green and says Ready")
    _, sa = page.evaluate(SV); time.sleep(0.5); _, sb = page.evaluate(SV)
    check(sa == sb, "SURVIVAL stops drawing while it is off screen")
    page.screenshot(path=os.path.join(shots, "23-garage-after-survival.png"))
    check(not [e for e in errors if "Content Security Policy" in e or "Refused" in e], "no CSP violation anywhere in the garage, the arcade or SURVIVAL")
    check(not errors, f"no console error, page error or failed request ({errors[:3]})")
    browser.close()
done()
