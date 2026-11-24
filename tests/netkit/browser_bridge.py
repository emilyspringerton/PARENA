"""A same-origin web server for the real-browser test: serves the page (markup from SHANKPIT/apps/netkit, generated JS + wasm from the
PARENA build) with the SAME Content-Security-Policy the Android shell sets, plus the stand-in for the Android shell's sockets: a
WebSocket-to-TCP bridge (/tcp), one-shot connect probes (/probe) and the in-page NetkitNative shim (/shim.js). Test-only code."""
import asyncio, json, os, socket, threading, time
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
    arpText() { return ''; },
    localNets() { return JSON.stringify({ cidrs: ['127.0.0.0/29'], ips: ['127.0.0.1'] }); },
    storeGet(k) { return store.has(k) ? store.get(k) : ''; }, storeSet(k, v) { store.set(k, v); }, storeDel(k) { store.delete(k); },
    clipboardSet(t) { clip = t; window.__clip = t; }, clipboardGet() { return window.__clipIn !== undefined ? window.__clipIn : clip; },
  };
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
    async def shim(request): return web.Response(text=SHIM, content_type="text/javascript")
    async def static(request):
        name = request.match_info.get("name") or "index.html"
        base = html_dir if name in ("index.html", "netkit.css") else web_dir
        path = os.path.join(base, os.path.basename(name))
        if not os.path.exists(path): return web.Response(status=404)
        ctype = {"html": "text/html", "css": "text/css", "js": "text/javascript", "wasm": "application/wasm"}.get(name.rsplit(".", 1)[-1], "application/octet-stream")
        body = open(path, "rb").read()
        if name == "index.html":      # the shim goes in before the page's own module script runs
            body = body.replace(b"<script type=\"module\" src=\"nk-host.js\">", b"<script src=\"/shim.js\"></script>\n<script type=\"module\" src=\"nk-host.js\">")
        return web.Response(body=body, content_type=ctype, headers={"Content-Security-Policy": CSP, "Cache-Control": "no-store"})
    app = web.Application()
    app.router.add_get("/tcp", tcp); app.router.add_get("/probe", probe); app.router.add_get("/shim.js", shim)
    app.router.add_get("/", static); app.router.add_get("/{name}", static)
    return app

class Site:
    def __init__(self, html_dir, web_dir):
        self.port = None; self.ready = threading.Event(); self.loop = asyncio.new_event_loop()
        threading.Thread(target=self._run, args=(html_dir, web_dir), daemon=True).start(); self.ready.wait(10)
    def _run(self, html_dir, web_dir):
        asyncio.set_event_loop(self.loop)
        runner = web.AppRunner(make_app(html_dir, web_dir)); self.loop.run_until_complete(runner.setup())
        s = socket.socket(); s.bind(("127.0.0.1", 0)); self.port = s.getsockname()[1]; s.close()
        self.loop.run_until_complete(web.TCPSite(runner, "127.0.0.1", self.port).start()); self.ready.set(); self.loop.run_forever()
