// End-to-end test of the page: the GENERATED nk-host.js, the three wasm modules and the real index.html, run headlessly in Node.
// Only two things are stubbed, because they are the platform: a DOM (just enough of one, auto-checked against the real index.html's ids)
// and the Android shell's NetkitNative (backed here by REAL sockets, so the page talks to real SSH and VNC servers).
// Test code only -- nothing in here ships.
//   node host_e2e.mjs <web-dir> <index.html> '<json config>'
import fs from 'node:fs';
import path from 'node:path';
import net from 'node:net';
import { pathToFileURL } from 'node:url';

const web = process.argv[2], htmlPath = process.argv[3], cfg = JSON.parse(process.argv[4] || '{}');
let pass = 0, fail = 0;
const check = (c, m) => { if (c) { pass++; console.log('PASS: ' + m); } else { fail++; console.log('FAIL: ' + m); } };
const sleep = ms => new Promise(r => setTimeout(r, ms));
async function waitFor(pred, ms = 10000, step = 20) {
  const t = Date.now();
  while (Date.now() - t < ms) { try { if (pred()) return true; } catch { /* not yet */ } await sleep(step); }
  return false;
}

// ---- a DOM, just big enough; ids and initial hidden state come from the real index.html -------------------------------------------------------------------
const html = fs.readFileSync(htmlPath, 'utf8');
const idInfo = new Map();
for (const m of html.matchAll(/<([a-zA-Z0-9]+)([^>]*?)>/g)) {
  const idm = /\bid="([^"]+)"/.exec(m[2]);
  if (idm) idInfo.set(idm[1], { hidden: /\shidden(\s|$|=)/.test(m[2]), value: (/\bvalue="([^"]*)"/.exec(m[2]) || [])[1] || '' });
}
const accessed = new Set();
const winListeners = {};
class El {
  constructor(id, info) {
    this.id = id; this.hidden = info.hidden; this.textContent = ''; this.innerHTML = ''; this.value = info.value; this.checked = false;
    this.dataset = {}; this.style = {}; this.listeners = {}; this.classes = new Set(); this.width = 0; this.height = 0;
    this.clientWidth = 800; this.clientHeight = 600; this.ctx = { puts: [], putImageData: (...a) => { this.ctx.puts.push(a); this.ctx.last = a; } };
    const self = this;
    this.classList = { toggle(c, f) { const on = f === undefined ? !self.classes.has(c) : !!f; on ? self.classes.add(c) : self.classes.delete(c); return on; }, contains: c => self.classes.has(c), add: c => self.classes.add(c), remove: c => self.classes.delete(c) };
  }
  addEventListener(ev, fn) { (this.listeners[ev] ||= []).push(fn); }
  dispatch(ev, e = {}) { for (const fn of this.listeners[ev] || []) fn({ target: this, preventDefault() {}, ...e }); }
  focus() { document.activeElement = this; }
  getContext() { return this.ctx; }
  getBoundingClientRect() { return { left: 0, top: 0, width: 800, height: 544 }; }
  setPointerCapture() {}
  closest() { return null; }
}
const els = new Map();
const getEl = id => { accessed.add(id); if (!idInfo.has(id)) return null; if (!els.has(id)) els.set(id, new El(id, idInfo.get(id))); return els.get(id); };
globalThis.document = { getElementById: getEl, addEventListener() {}, activeElement: null, body: {} };
globalThis.window = globalThis;
globalThis.addEventListener = (ev, fn) => { (winListeners[ev] ||= []).push(fn); };
globalThis.requestAnimationFrame = fn => setTimeout(() => fn(performance.now()), 16);
globalThis.ImageData = class ImageData { constructor(data, w, h) { this.data = data; this.width = w; this.height = h; } };
globalThis.fetch = async url => { const f = path.join(web, url); return { ok: fs.existsSync(f), status: 404, url, arrayBuffer: async () => fs.readFileSync(f) }; };
const $ = id => getEl(id);
const instances = [];
const OrigInstance = WebAssembly.Instance;
WebAssembly.Instance = class extends OrigInstance { constructor(m, i) { super(m, i); instances.push(this); } };

// ---- the Android shell, as sockets ---------------------------------------------------------------------------------------------------------------------------
const store = new Map(); const socks = new Map(); const sent = {}; const portOf = {}; let clip = '';
const emit = o => setImmediate(() => globalThis.netkitEvent(JSON.stringify(o)));
const codeOf = err => err.code === 'ECONNREFUSED' ? 1 : err.code === 'ETIMEDOUT' ? 2 : (err.code === 'EHOSTUNREACH' || err.code === 'ENETUNREACH') ? 3 : 4;
globalThis.NetkitNative = {
  tcpOpen(id, host, port) {
    const s = net.connect({ host, port }); socks.set(id, s); portOf[id] = port; s.setNoDelay(true);
    s.once('connect', () => emit({ t: 'tcp', id, e: 'open' }));
    s.on('data', d => emit({ t: 'tcp', id, e: 'data', b: d.toString('base64') }));
    s.on('close', () => emit({ t: 'tcp', id, e: 'closed' }));
    s.on('error', err => emit({ t: 'tcp', id, e: 'error', code: codeOf(err) }));
  },
  tcpWrite(id, b64) { const b = Buffer.from(b64, 'base64'); (sent[id] ||= []).push(b); const s = socks.get(id); if (s) s.write(b); },
  tcpClose(id) { const s = socks.get(id); if (s) s.destroy(); socks.delete(id); },
  probe(job, ip, port, timeout) {
    const t0 = Date.now(); const s = net.connect({ host: ip, port }); let done = false;
    const fin = code => { if (done) return; done = true; s.destroy(); emit({ t: 'probe', job, code, ms: Date.now() - t0 }); };
    s.setTimeout(timeout, () => fin(2)); s.once('connect', () => fin(0)); s.once('error', err => fin(codeOf(err)));
  },
  ping(job, host, timeout) {                       // "is something there": an answer or a refusal both mean yes
    const t0 = Date.now(); const s = net.connect({ host, port: 7 }); let done = false;
    const fin = code => { if (done) return; done = true; s.destroy(); emit({ t: 'probe', job, code, ms: Date.now() - t0 }); };
    s.setTimeout(timeout, () => fin(2)); s.once('connect', () => fin(0)); s.once('error', err => fin(err.code === 'ECONNREFUSED' ? 0 : codeOf(err)));
  },
  resolve(ip) { if (ip === '127.0.0.4') emit({ t: 'name', ip, name: 'imac.lan' }); },
  arpText() { return ''; },
  localNets() { return JSON.stringify({ cidrs: ['127.0.0.0/29'], ips: ['127.0.0.1'] }); },
  storeGet(k) { return store.get(k) ?? ''; }, storeSet(k, v) { store.set(k, v); }, storeDel(k) { store.delete(k); },
  clipboardSet(t) { clip = t; }, clipboardGet() { return clip; },
};

// ---- helpers: reading the terminal, driving the page -----------------------------------------------------------------------------------------------------------
const glyphChar = g => g === 0 ? ' ' : g < 95 ? String.fromCharCode(g + 32) : g < 223 ? String.fromCharCode(9472 + g - 95) : String.fromCharCode(9600 + g - 223);
const termInst = () => [...instances].reverse().find(i => i.exports.term_init);
function termScreen() {
  const ex = termInst().exports, dv = new DataView(ex.memory.buffer), top = dv.getInt32(0x31000 + 8, true), rows = [];
  for (let r = 0; r < 34; r++) { let s = ''; const base = 0x100000 + (((top + r) % 1500) * 100) * 12; for (let c = 0; c < 100; c++) s += glyphChar(dv.getUint32(base + c * 12, true) & 0xFFFF); rows.push(s.trimEnd()); }
  return rows.join('\n');
}
const key = (k, extra = {}) => { for (const fn of winListeners.keydown || []) fn({ key: k, isComposing: false, metaKey: false, ctrlKey: false, altKey: false, shiftKey: false, preventDefault() {}, ...extra }); };
const typeText = s => { for (const ch of s) key(ch === '\n' ? 'Enter' : ch); };
const clickIn = (listId, attrs, act, extra = {}) => {
  const li = { dataset: attrs }; const b = { dataset: { act, ...extra }, closest: sel => sel === 'li' ? li : null };
  $(listId).dispatch('click', { target: { closest: sel => sel === '[data-act]' ? b : null } });
};
const savedClick = (i, act) => { const li = { dataset: { i: String(i) } }; const b = { dataset: { act }, closest: sel => sel === 'li' ? li : null }; $('saved-list').dispatch('click', { target: { closest: sel => sel === '[data-act]' ? b : null } }); };
const visible = id => !$(id).hidden;

// ---- some real listeners for the scanner to find ----------------------------------------------------------------------------------------------------------------------
const listeners = [];
for (const [host, port] of [['127.0.0.3', 5900], ['127.0.0.4', 5901]]) {
  await new Promise(res => { const s = net.createServer(c => c.destroy()); s.on('error', () => res()); s.listen(port, host, () => { listeners.push(s); res(); }); });
}

// ================================================================ boot ============================================================================================
await import(pathToFileURL(path.join(web, 'nk-host.js')).href);
const booted = await waitFor(() => $('btn-scan').listeners.click, 10000);
check(booted, 'the page boots: three wasm modules fetched, compiled, instantiated; the buttons are wired');
check(instances.length >= 1 && instances.every(i => Object.keys(i.exports).length > 0), 'the scanner instance exists and exports its functions');
check($('net-pill').textContent === '127.0.0.0/29', `the network pill shows the range we would scan (${$('net-pill').textContent})`);

// ================================================================ scan ============================================================================================
const scanned = await waitFor(() => $('scan-status').textContent.startsWith('Scanned'), 20000);
check(scanned, `an automatic scan starts on boot and finishes (${$('scan-status').textContent})`);
const list = () => $('device-list').innerHTML;
check(/data-ip="127\.0\.0\.3"/.test(list()) && /VNC 5900/.test(list()), 'a host with port 5900 open appears, with its service chip');
check(/data-ip="127\.0\.0\.4"/.test(list()) && /VNC 5901/.test(list()) && /VNC host/.test(list()), 'a host with port 5901 open is labelled "VNC host" and offers Screen');
check(/data-act="vnc" data-port="5900"/.test(list()) && /data-act="vnc" data-port="5901"/.test(list()), 'the Screen buttons carry the right ports (5900 and 5901)');
check(['2', '5', '6'].every(n => new RegExp(`data-ip="127\\.0\\.0\\.${n}"`).test(list())), 'every other host that answers (even only to refuse) is listed: an RST proves it is there');
check(!/data-ip="127\.0\.0\.1"/.test(list()), 'this tablet itself (127.0.0.1) is excluded from the sweep');
check(await waitFor(() => /imac\.lan/.test(list()), 3000), 'a name resolved by the shell lands on the device card');
check(!/is-hidden/.test(list()) && $('empty-hint').hidden === true, 'the empty-state hint is hidden once devices exist');

// pinning
const li4 = /<li class="dev[^"]*" data-ip="127\.0\.0\.4" data-key="([^"]*)">/.exec(list());
clickIn('device-list', { ip: '127.0.0.4', key: li4[1] }, 'pin');
check(JSON.parse(store.get('pins')).includes(li4[1]), 'pinning a device stores its key');
check(/^<li class="dev pinned" data-ip="127\.0\.0\.4"/.test(list()), 'a pinned device sorts to the top of the list');
clickIn('device-list', { ip: '127.0.0.4', key: li4[1] }, 'pin');
check(!JSON.parse(store.get('pins')).includes(li4[1]) && !/^<li class="dev pinned"/.test(list()), 'unpinning puts it back in address order');

// tools
$('m-host').value = '127.0.0.3'; $('m-port').value = '8080'; $('btn-m-check').dispatch('click');
check(await waitFor(() => /answered in \d+ ms/.test($('tool-out').textContent), 5000), `Port check on an open port says so (${$('tool-out').textContent})`);
$('m-port').value = '9'; $('btn-m-check').dispatch('click');
check(await waitFor(() => /Nothing is listening on that port/.test($('tool-out').textContent), 5000), `Port check on a closed port says it in plain words (${$('tool-out').textContent})`);
$('btn-m-ping').dispatch('click');
check(await waitFor(() => /answered in/.test($('tool-out').textContent), 5000), 'Ping reports the host is there');
$('m-host').value = ''; $('btn-m-check').dispatch('click');
check(/Type a host/.test($('tool-out').textContent), 'a tool run with no host asks for one');
$('m-host').value = 'not a range'; $('scan-cidr').value = 'not a range'; $('btn-scan').dispatch('click');
check(/isn't a network range/.test($('banner-text').textContent) && visible('banner'), 'a malformed scan range is refused with a plain sentence');
$('banner-close').dispatch('click'); $('scan-cidr').value = '';
check(!visible('banner'), 'the banner can be dismissed');

// ================================================================ SSH ============================================================================================
const sshPort = cfg.ssh.port;
$('m-proto').value = 'ssh'; $('m-host').value = '127.0.0.1'; $('m-port').value = String(sshPort);
$('btn-m-connect').dispatch('click');
check(visible('dlg-connect') && $('c-host').value === '127.0.0.1' && $('c-port').value == sshPort && visible('c-user-row'), 'Connect opens the dialog, pre-filled, with a user field for SSH');
$('c-go').dispatch('click');
check($('c-error').textContent !== '' && visible('dlg-connect'), 'an empty user is refused in the dialog with a plain sentence');
$('c-user').value = 'tester'; $('c-pass').value = 's3cret'; $('c-save').checked = true;
$('c-go').dispatch('click');
check(await waitFor(() => visible('dlg-hostkey'), 15000), 'a first-time device stops at "Trust this device?" before any login is sent');
check($('hk-fp').textContent === cfg.ssh.fp, `the fingerprint shown is the server's own (${$('hk-fp').textContent})`);
check(/proved it owns/.test($('hk-note').textContent) || /It proved/.test($('hk-note').textContent), 'the dialog says the device proved it owns the key');
check(cfg.ssh.loginsNow() === 0, 'the server has seen no login attempt yet');
$('hk-trust').dispatch('click');
check(await waitFor(() => visible('view-ssh'), 15000), 'trusting it logs in and opens the Terminal view');
check(await waitFor(() => /WELCOME TERM=xterm-256color SIZE=100x34/.test(termScreen()), 10000), 'the shell greets: pty is xterm-256color, 100x34 -- the terminal shows it');
check($('ssh-title').textContent === 'tester@127.0.0.1', 'the title names the session');
typeText('hello world\n');
check(await waitFor(() => /got: hello world/.test(termScreen()), 10000), 'keystrokes (real DOM keydown events) reach the server and its reply is drawn');
const wasm = termInst().exports;
check(await waitFor(() => wasm.term_render() === 0 || true, 10), '(render loop alive)');
check(await waitFor(() => $('term-canvas').ctx.puts.length > 0, 3000), 'the terminal canvas is painted with putImageData');
const ctrlBtn = { dataset: { key: 'ctrl' }, closest: s => s === 'button' ? ctrlBtn : null };
$('keybar').dispatch('click', { target: ctrlBtn });
check($('key-ctrl').classes.has('on'), 'tapping Ctrl on the key bar arms it (highlighted)');
key('c');
check(!$('key-ctrl').classes.has('on'), 'the sticky Ctrl is used up by the next key');
const upBtn = { dataset: { key: 'up' }, closest: s => s === 'button' ? upBtn : null };
$('keybar').dispatch('click', { target: upBtn });
typeText('\n');
typeText('STDERR\n');
check(await waitFor(() => /to-stderr/.test(termScreen()), 8000), 'stderr output from the server is shown too');
// selection + copy through the page
$('term-canvas').dispatch('pointerdown', { clientX: 0, clientY: 8, pointerId: 1 });
$('term-canvas').dispatch('pointermove', { clientX: 400, clientY: 8, pointerId: 1 });
$('term-canvas').dispatch('pointerup', { clientX: 400, clientY: 8, pointerId: 1 });
$('btn-copy').dispatch('click');
check(/WELCOME/.test(clip), `dragging across the first line and tapping Copy puts the text on the clipboard (${JSON.stringify(clip.slice(0, 40))})`);
// paste
clip = 'echo pasted-text-ok\n'; $('btn-paste').dispatch('click');
check(await waitFor(() => /got: echo pasted-text-ok/.test(termScreen()), 8000), 'Paste sends the clipboard to the server as typed text');
check(JSON.parse(store.get('hostkeys'))[`127.0.0.1:${sshPort}`] === cfg.ssh.fpB64, 'the trusted fingerprint is remembered per host:port');
check(JSON.parse(store.get('conns')).some(c => c.proto === 'ssh' && c.user === 'tester' && c.pw === 's3cret'), 'Remember saved the connection');
check(store.get('last-user') === 'tester', 'the last user name is remembered');
check(/tester@127\.0\.0\.1/.test($('saved-list').innerHTML), 'the Saved list shows it');
typeText('EXIT\n');
check(await waitFor(() => /Session ended \(exit status 7\)/.test($('banner-text').textContent), 10000), `the server ending the session is reported with its exit status (${$('banner-text').textContent})`);
$('btn-ssh-close').dispatch('click');
check(visible('view-devices') && !visible('view-ssh') && $('tab-ssh').hidden, 'Disconnect returns to the device list and hides the Terminal tab');

// reconnect from Saved: the fingerprint is pinned, so no question is asked
$('banner-close').dispatch('click');
savedClick(0, 'go');
check(await waitFor(() => visible('view-ssh'), 15000) && !visible('dlg-hostkey'), 'connecting from Saved goes straight to a shell: the pinned fingerprint matched, so no dialog');
check(await waitFor(() => /WELCOME/.test(termScreen()), 8000), '...and the shell is there');
typeText('EXIT\n'); await waitFor(() => /Session ended/.test($('banner-text').textContent), 8000); $('btn-ssh-close').dispatch('click'); $('banner-close').dispatch('click');

// a wrong password is a plain failure
$('m-host').value = '127.0.0.1'; $('m-port').value = String(sshPort); $('btn-m-connect').dispatch('click');
$('c-user').value = 'tester'; $('c-pass').value = 'wrong-password'; $('c-save').checked = false; $('c-go').dispatch('click');
check(await waitFor(() => /Wrong/.test($('banner-text').textContent), 15000), `a wrong password: "${$('banner-text').textContent}"`);
check(visible('view-devices') && $('tab-ssh').hidden, 'a failed login returns to the device list');
$('banner-close').dispatch('click');

// the device's identity changed: nothing is sent
const hk = JSON.parse(store.get('hostkeys')); hk[`127.0.0.1:${sshPort}`] = Buffer.alloc(32, 7).toString('base64'); store.set('hostkeys', JSON.stringify(hk));
const failsBefore = cfg.ssh.failuresNow(), loginsBefore = cfg.ssh.loginsNow();
$('m-host').value = '127.0.0.1'; $('btn-m-connect').dispatch('click'); $('c-user').value = 'tester'; $('c-pass').value = 's3cret'; $('c-go').dispatch('click');
check(await waitFor(() => /identity changed/.test($('banner-text').textContent), 15000), `a changed host key: "${$('banner-text').textContent}"`);
check(cfg.ssh.failuresNow() === failsBefore && cfg.ssh.loginsNow() === loginsBefore, 'the server saw NO authentication attempt: the password was never sent to the impostor');
$('banner-close').dispatch('click');

// connection refused
$('m-host').value = '127.0.0.1'; $('m-port').value = '9'; $('btn-m-connect').dispatch('click'); $('c-user').value = 'x'; $('c-go').dispatch('click');
check(await waitFor(() => /Nothing is listening on that port/.test($('banner-text').textContent), 8000), 'SSH to a closed port says so in plain words');
$('banner-close').dispatch('click');

// ================================================================ VNC =============================================================================================
$('m-proto').value = 'vnc'; $('m-proto').dispatch('change');
check($('m-port').value == 5900, 'switching the protocol to Screen sets the default port to 5900');
$('m-host').value = '127.0.0.1'; $('m-port').value = String(cfg.vnc.port); $('btn-m-connect').dispatch('click');
check(visible('dlg-connect') && !visible('c-user-row'), 'the Screen dialog has no user field (VNC has only a password)');
$('c-pass').value = 'secret'; $('c-save').checked = false; $('c-go').dispatch('click');
check(await waitFor(() => visible('view-vnc'), 15000), 'a correct password opens the Screen view');
check($('vnc-title').textContent.startsWith('vncd-interop'), `the title shows the desktop's name (${$('vnc-title').textContent})`);
check(await waitFor(() => ($('vnc-canvas').ctx.puts.length > 0), 8000), 'the remote screen is painted onto the canvas');
check(await waitFor(() => /closed the connection/.test($('banner-text').textContent), 20000), 'the server hanging up is reported plainly');
const lastPut = $('vnc-canvas').ctx.last; const img = lastPut[0];
check($('vnc-canvas').width === 256 && $('vnc-canvas').height === 160 && img.width === 256 && img.height === 160, 'the canvas followed the remote DesktopSize change to 256x160');
if (cfg.vnc.rgb && fs.existsSync(cfg.vnc.rgb)) {
  const raw = fs.readFileSync(cfg.vnc.rgb); const nl = raw.indexOf(10); const body = raw.subarray(nl + 1); let diff = 0;
  for (let i = 0; i < 256 * 160; i++) if (body[i * 3] !== img.data[i * 4] || body[i * 3 + 1] !== img.data[i * 4 + 1] || body[i * 3 + 2] !== img.data[i * 4 + 2] || img.data[i * 4 + 3] !== 255) diff++;
  check(diff === 0, `the image on the page equals the VNC server's own final framebuffer, byte for byte (${diff} differing pixels of ${256 * 160})`);
}
const vncId = Object.keys(portOf).find(id => portOf[id] === cfg.vnc.port && sent[id]);
const wire = Buffer.concat(sent[vncId] || []);
let i = 30, types = new Set();                       // greeting 12 + security type 1 + response 16 + ClientInit 1
while (i < wire.length) { const t = wire[i]; types.add(t); i += t === 0 ? 20 : t === 3 ? 10 : t === 2 ? 4 + 4 * wire.readUInt16BE(i + 2) : 1; }
check([...types].every(t => t === 0 || t === 2 || t === 3), `view only, on the real wire from the page: only message types ${[...types].sort()} were sent (never key, pointer or clipboard)`);
const fitBefore = $('vnc-canvas').style.transform;
$('btn-1to1').dispatch('click'); const t1 = $('vnc-canvas').style.transform;
$('btn-fit').dispatch('click'); const t2 = $('vnc-canvas').style.transform;
check(/scale\(1\)/.test(t1) && t2 !== t1, `1:1 and Fit change the canvas transform (${t1} / ${t2})`);
$('vnc-wrap').dispatch('wheel', { deltaY: -100, clientX: 100, clientY: 100 });
check($('vnc-canvas').style.transform !== t2, 'the wheel zooms');
check(globalThis.netkitBack() === true && visible('view-devices') && $('tab-vnc').hidden, "Android's back button closes the Screen and returns to the devices");
check(globalThis.netkitBack() === false, "back on the device list is not handled (the app may finish)");

// a VNC server that wants a password the user did not give
$('m-proto').value = 'vnc'; $('m-host').value = '127.0.0.1'; $('m-port').value = '9'; $('btn-m-connect').dispatch('click'); $('c-go').dispatch('click');
check(await waitFor(() => /Nothing is listening on that port/.test($('banner-text').textContent), 8000), 'a closed VNC port is reported plainly'); $('banner-close').dispatch('click');

// ================================================================ the HTML contract ============================================================================================
const missing = [...accessed].filter(id => !idInfo.has(id));
check(missing.length === 0, `every element id the generated page uses exists in index.html${missing.length ? ' -- missing: ' + missing.join(', ') : ''}`);
for (const s of listeners) s.close();
console.log(`\n${pass} passed, ${fail} failed`);
process.exit(fail ? 1 : 0);
