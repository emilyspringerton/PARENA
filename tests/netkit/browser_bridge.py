"""A same-origin web server for the real-browser test: serves the page (markup from SHANKPIT/apps/netkit, generated JS + wasm from the
PARENA build) with the SAME Content-Security-Policy the Android shell sets, plus the stand-in for the Android shell's sockets: a
WebSocket-to-TCP bridge (/tcp), one-shot connect probes (/probe) and the in-page NetkitNative shim (/shim.js). Test-only code."""
import asyncio, base64, struct, json, os, socket, threading, time
from aiohttp import web, WSMsgType

CSP = ("default-src 'none'; script-src 'self' 'wasm-unsafe-eval'; style-src 'self' 'unsafe-inline'; img-src 'self' data:; "
       "connect-src 'self'; base-uri 'none'; form-action 'none'")

SHIM = r"""
(function () {
  const store = new Map(); const socks = new Map(); let clip = '';
  const emit = o => setTimeout(() => window.netkitEvent && window.netkitEvent(JSON.stringify(o)), 0);
  const b64 = u8 => { let s = ''; for (let i = 0; i < u8.length; i += 8192) s += String.fromCharCode.apply(null, u8.subarray(i, i + 8192)); return btoa(s); };
  window.NetkitNative = {
    tcpOpen(id, host, port, timeout) {
      const ws = new WebSocket(`ws://${location.host}/tcp?host=${encodeURIComponent(host)}&port=${port}&timeout=${timeout}`);
      ws.binaryType = 'arraybuffer'; const q = []; socks.set(id, { ws, q });
      ws.onopen = () => { for (const b of q) ws.send(b); q.length = 0; };
      ws.onmessage = ev => {
        if (typeof ev.data === 'string') {
          if (ev.data === 'open') emit({ t: 'tcp', id, e: 'open' });
          else if (ev.data === 'closed') emit({ t: 'tcp', id, e: 'closed' });
          else if (ev.data.startsWith('error:')) emit({ t: 'tcp', id, e: 'error', code: +ev.data.slice(6) });
        } else emit({ t: 'tcp', id, e: 'data', b: b64(new Uint8Array(ev.data)) });
      };
    },
    tcpWrite(id, b) { const s = socks.get(id); if (!s) return; const bin = atob(b); const u8 = new Uint8Array(bin.length); for (let i = 0; i < bin.length; i++) u8[i] = bin.charCodeAt(i); s.ws.readyState === 1 ? s.ws.send(u8) : s.q.push(u8); },
    tcpClose(id) { const s = socks.get(id); if (s) { try { s.ws.close(); } catch (e) {} socks.delete(id); } },
    probe(job, ip, port, timeout) { fetch(`/probe?host=${ip}&port=${port}&timeout=${timeout}`).then(r => r.json()).then(j => emit({ t: 'probe', job, code: j.code, ms: j.ms })); },
    ping(job, host, timeout) { fetch(`/probe?host=${host}&port=7&timeout=${timeout}&alive=1`).then(r => r.json()).then(j => emit({ t: 'probe', job, code: j.code, ms: j.ms })); },
    resolve(ip) { if (ip === '127.0.0.4') emit({ t: 'name', ip, name: 'imac.lan' }); },
    udpQuery(id, host, port, b, timeout) { fetch(`/udp?host=${host}&port=${port}&timeout=${timeout}&b=${encodeURIComponent(b)}`).then(r => r.json()).then(j => emit(j.b ? { t: 'udp', id, e: 'data', b: j.b } : { t: 'udp', id, e: 'none' })); },
    arpText() { return ''; },
    localNets() { return JSON.stringify({ cidrs: ['127.0.0.0/29'], ips: ['127.0.0.1'] }); },
    storeGet(k) { return store.has(k) ? store.get(k) : ''; }, storeSet(k, v) { store.set(k, v); }, storeDel(k) { store.delete(k); },
    clipboardSet(t) { clip = t; window.__clip = t; }, clipboardGet() { return window.__clipIn !== undefined ? window.__clipIn : clip; },
    // the USB drive (DriveStore.java): an in-memory drive the test fills and reads back through window.__drive (path -> base64)
    driveState() { return JSON.stringify({ open: window.__driveOpen ? 1 : 0, name: window.__driveOpen ? 'TESTSTICK' : '' }); },
    driveOpen() { window.__driveOpen = true; emit({ t: 'drive', e: 'open', name: 'TESTSTICK' }); },
    driveForget() { window.__driveOpen = false; },
    driveList(path) {
      const d = window.__drive, pre = path === '' ? '' : path + '/', seen = new Map();
      for (const k of Object.keys(d)) if (k.startsWith(pre)) { const rest = k.slice(pre.length), i = rest.indexOf('/');
        if (i < 0) seen.set(rest, { n: rest, d: 0, s: atob(d[k]).length, m: 0 }); else seen.set(rest.slice(0, i), { n: rest.slice(0, i), d: 1, s: -1, m: 0 }); }
      emit({ t: 'drive', e: 'list', path, items: [...seen.values()] });
    },
    driveRead(path) { const b = window.__drive[path]; emit(b === undefined ? { t: 'drive', e: 'error', op: 'read', path, msg: 'Not found (is the drive still plugged in?)' } : { t: 'drive', e: 'read', path, b }); },
    driveWrite(path, b) { window.__drive[path] = b; window.__writes = (window.__writes || 0) + 1; emit({ t: 'drive', e: 'wrote', path, n: atob(b).length, name: 'TESTSTICK' }); },
  };
  window.__drive = window.__drive || {};
})();
"""

def code_of(exc):
    if isinstance(exc, ConnectionRefusedError): return 1
    if isinstance(exc, asyncio.TimeoutError): return 2
    if isinstance(exc, OSError) and exc.errno in (101, 113): return 3
    return 4

async def connect(host, port, timeout_ms):
    return await asyncio.wait_for(asyncio.open_connection(host, port), timeout_ms / 1000)

def make_app(html_dir, web_dir):
    async def tcp(request):
        ws = web.WebSocketResponse(); await ws.prepare(request)
        host, port, tmo = request.query["host"], int(request.query["port"]), int(request.query.get("timeout", 8000))
        try: r, w = await connect(host, port, tmo)
        except Exception as e:
            await ws.send_str(f"error:{code_of(e)}"); await ws.close(); return ws
        await ws.send_str("open")
        async def pump():
            try:
                while (d := await r.read(16384)): await ws.send_bytes(d)
            except Exception: pass
            if not ws.closed:
                await ws.send_str("closed"); await ws.close()
        t = asyncio.ensure_future(pump())
        async for m in ws:
            if m.type == WSMsgType.BINARY: w.write(m.data); await w.drain()
            elif m.type in (WSMsgType.CLOSE, WSMsgType.ERROR): break
        t.cancel(); w.close()
        return ws
    async def probe(request):
        host, port, tmo = request.query["host"], int(request.query["port"]), int(request.query.get("timeout", 900))
        t0 = time.time()
        try:
            r, w = await connect(host, port, tmo); w.close(); code = 0
        except Exception as e:
            code = 0 if (request.query.get("alive") and code_of(e) == 1) else code_of(e)
        return web.json_response({"code": code, "ms": int((time.time() - t0) * 1000)})
    async def udp(request):      # one real UDP question, as Udp.java (NetBIOS's privileged 137 is served on 10137 in tests)
        host, port, tmo = request.query["host"], int(request.query["port"]), int(request.query.get("timeout", 1500))
        pkt = base64.b64decode(request.query["b"])
        def ask():
            s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); s.settimeout(tmo / 1000)
            try:
                s.sendto(pkt, (host, 10137 if port == 137 else port))
                while True:
                    d, a = s.recvfrom(1536)
                    if a[0] == host: return base64.b64encode(d).decode()
            except OSError: return None
            finally: s.close()
        return web.json_response({"b": await asyncio.get_running_loop().run_in_executor(None, ask)})
    async def shim(request): return web.Response(text=SHIM, content_type="text/javascript")
    async def static(request):
        name = request.match_info.get("name") or "index.html"
        if request.path.startswith("/kit/"): name = "kit/" + os.path.basename(name)
        base = html_dir if name in ("index.html", "netkit.css") else web_dir
        path = os.path.join(base, name if name.startswith("kit/") else os.path.basename(name))
        if not os.path.exists(path): return web.Response(status=404)
        ctype = {"html": "text/html", "css": "text/css", "js": "text/javascript", "wasm": "application/wasm", "png": "image/png"}.get(name.rsplit(".", 1)[-1], "application/octet-stream")
        body = open(path, "rb").read()
        if name == "index.html":      # the shim goes in before the page's own module script runs
            body = body.replace(b"<script type=\"module\" src=\"nk-host.js\">", b"<script src=\"/shim.js\"></script>\n<script type=\"module\" src=\"nk-host.js\">")
        return web.Response(body=body, content_type=ctype, headers={"Content-Security-Policy": CSP, "Cache-Control": "no-store"})
    app = web.Application()
    app.router.add_get("/tcp", tcp); app.router.add_get("/probe", probe); app.router.add_get("/udp", udp); app.router.add_get("/shim.js", shim)
    app.router.add_get("/", static); app.router.add_get("/kit/{name}", static); app.router.add_get("/{name}", static)
    return app

def udp_responders():
    """Real UDP name responders written from the RFCs (independent of the PARENA parser): 127.0.0.5 is a Raspberry Pi whose avahi answers
    a reverse lookup with raspberrypi.local (RFC 6762 6.7); 127.0.0.6 is a PC answering NetBIOS node status (RFC 1002) as DESKTOP-7Q2 with
    an Apple-made card. Returns the sockets (daemon threads serve them)."""
    def mdns(q):
        rd = bytes([11]) + b"raspberrypi" + bytes([5]) + b"local\0"
        return (q[:2] + b"\x84\x00" + q[4:6] + b"\x00\x01\x00\x00\x00\x00" + q[12:] + b"\xc0\x0c"
                + struct.pack(">HHIH", 12, 0x8001, 120, len(rd)) + rd)
    def nbns(q):
        ent = lambda n, suf, fl: n.ljust(15, b" ") + bytes([suf]) + struct.pack(">H", fl)
        names = [ent(b"WORKGROUP", 0, 0x8400), ent(b"DESKTOP-7Q2", 0, 0x0400)]
        rd = bytes([len(names)]) + b"".join(names) + bytes.fromhex("f01898aabbcc") + bytes(40)
        return q[:2] + b"\x84\x00\x00\x00\x00\x01\x00\x00\x00\x00" + q[12:46] + struct.pack(">HHIH", 0x21, 1, 0, len(rd)) + rd
    socks = []
    for host, port, fn in (("127.0.0.5", 5353, mdns), ("127.0.0.6", 10137, nbns)):
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        try: s.bind((host, port))
        except OSError: continue
        def serve(s=s, fn=fn):
            while True:
                try: q, a = s.recvfrom(1536); s.sendto(fn(q), a)
                except OSError: return
        threading.Thread(target=serve, daemon=True).start(); socks.append(s)
    return socks

class Site:
    def __init__(self, html_dir, web_dir):
        self.port = None; self.ready = threading.Event(); self.loop = asyncio.new_event_loop()
        threading.Thread(target=self._run, args=(html_dir, web_dir), daemon=True).start(); self.ready.wait(10)
    def _run(self, html_dir, web_dir):
        asyncio.set_event_loop(self.loop)
        runner = web.AppRunner(make_app(html_dir, web_dir)); self.loop.run_until_complete(runner.setup())
        s = socket.socket(); s.bind(("127.0.0.1", 0)); self.port = s.getsockname()[1]; s.close()
        self.loop.run_until_complete(web.TCPSite(runner, "127.0.0.1", self.port).start()); self.ready.set(); self.loop.run_forever()
