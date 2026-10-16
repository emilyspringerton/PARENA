// Behavioral test for stdlib/gfd/term.prn compiled to wasm. Usage: node gfd_term_test.mjs term.wasm
import fs from 'node:fs';
const { instance } = await WebAssembly.instantiate(fs.readFileSync(process.argv[2]), {});
const x = instance.exports, mem = () => new Uint8Array(x.memory.buffer);
let pass = 0, fail = 0;
const ok = (c, m) => { c ? pass++ : fail++; console.log((c ? 'PASS: ' : 'FAIL: ') + m); };

const feed = (s) => { const b = Buffer.from(s, 'utf8'); mem().set(b, x.term_in_addr()); x.term_feed(b.length); };
const cell = (line, col) => { const dv = new DataView(x.memory.buffer); const a = 1048576 + ((line % 1500) * 100 + col) * 12;
  return { w: dv.getUint32(a, true), fg: dv.getUint32(a + 4, true), bg: dv.getUint32(a + 8, true) }; };
const glyph = (l, c) => cell(l, c).w & 0xffff;
const rowText = (l) => { let s = ''; for (let c = 0; c < 100; c++) { const g = glyph(l, c); s += g < 95 ? String.fromCharCode(g + 32) : '?'; } return s.trimEnd(); };
const out = (n) => Buffer.from(mem().slice(x.term_out_addr(), x.term_out_addr() + n));
const key = (c, m = 0) => out(x.term_key(c, m));
const ENTER = 1114113, BS = 1114114, UP = 1114115, DOWN = 1114116, PGUP = 1114117, PGDN = 1114118;

x.term_init();
ok(x.term_cols() === 100 && x.term_rows() === 34, 'geometry 100x34');
ok(mem()[204800 + 33 * 16 + 5] !== 0, 'font loaded (glyph "A" has pixels)');
ok(x.pal(1) === 4936677, 'palette red entry');

feed('Hello\r\nWorld');
ok(rowText(0) === 'Hello' && rowText(1) === 'World', 'text and CRLF land on the right rows');
ok(x.st(0) === 5 && x.st(1) === 1, 'cursor after "World" is (5,1)');

feed('\x1b[31mR\x1b[0mN');
let r = cell(1, 5), n = cell(1, 6);
ok(r.fg === 4936677, 'SGR 31 sets red foreground');
ok(n.fg === 15918054, 'SGR 0 resets to default foreground');
feed('\x1b[1;4mB\x1b[0m');
ok((cell(1, 7).w >>> 16 & 3) === 3, 'bold+underline flags set');
feed('\x1b[38;5;196mX\x1b[38;2;10;20;30mY\x1b[0m');
ok(cell(1, 8).fg === x.pal(196), '256-colour foreground');
ok(cell(1, 9).fg === (10 | 20 << 8 | 30 << 16), 'truecolour foreground');

feed('\x1b[2J\x1b[H');
ok(rowText(0) === '' && x.st(0) === 0 && x.st(1) === 0, 'clear screen + home');
feed('abc\x1b[2D\x1b[1Cd');
ok(rowText(0) === 'abd', 'cursor back 2, forward 1, overwrite');
feed('\r\x1b[K'); ok(rowText(0) === '', 'erase to end of line');
feed('\x1b[5;10Hz'); ok(glyph(4, 9) === 'z'.charCodeAt(0) - 32, 'absolute cursor position (CSI H)');

feed('\x1b[2J\x1b[H─│█'); ok(glyph(0, 0) === 95 && glyph(0, 1) === 95 + 2 && glyph(0, 2) === 223 + 8, 'UTF-8 box drawing and block glyphs');
feed('\x1b[2J\x1b[Hé\xff'); ok(glyph(0, 0) === 31, 'unmapped codepoint shows "?"');
feed('\x1b[2J\x1b[H'); feed('x'.repeat(100)); ok(x.st(1) === 0 && x.st(25) === 1, '100th column sets pending wrap');
feed('y'); ok(x.st(1) === 1 && glyph(1, 0) === 'y'.charCodeAt(0) - 32, 'next char wraps to the next line');

feed('\x1b]0;window title\x07ok'); ok(rowText(1).includes('yok'), 'OSC title sequence is swallowed');

x.term_init();
for (let i = 0; i < 60; i++) feed(`line ${i}\r\n`);
ok(x.st(2) > 0, 'screen scrolled (top advanced)');
const topNow = x.st(2);
ok(rowText(topNow) === `line ${topNow}`, 'scrolled rows keep their content');
x.term_scroll(10); ok(x.st(3) === 10, 'scrollback view moves');
feed('more\r\n'); ok(x.st(3) === 11, 'new output keeps a scrolled-back view in place');
x.term_scroll(1 << 20); ok(x.st(3) <= x.max_view(), 'scrollback clamps');
x.term_key(120, 0); ok(x.st(3) === 0, 'typing snaps back to the live view');

// ---- keys, line tracking, history ----
x.term_init();
ok(key('l'.charCodeAt(0)).toString() === 'l', 'printable key -> its byte');
key('o'.charCodeAt(0)); key('o'.charCodeAt(0)); key('k'.charCodeAt(0));
ok(x.st(20) === 4, 'typed line length tracked');
ok(key(BS).equals(Buffer.from([127])) && x.st(20) === 3, 'backspace -> DEL and shortens line');
key('k'.charCodeAt(0));
ok(key(ENTER).equals(Buffer.from([13])) && x.st(20) === 0, 'enter -> CR and clears the line');
for (const ch of 'north') key(ch.charCodeAt(0)); key(ENTER);
for (const ch of 'north') key(ch.charCodeAt(0)); key(ENTER);
ok(x.st(21) === 2, 'consecutive duplicates are not stored twice');
let up = key(UP); ok(up.equals(Buffer.from('north')), 'UP recalls the last command');
for (const ch of 'abc') key(ch.charCodeAt(0));
up = key(UP);
ok(up.equals(Buffer.concat([Buffer.alloc(8, 127), Buffer.from('north')])), 'typing resets the history cursor: UP replaces the typed text with the newest entry');
up = key(UP);
ok(up.equals(Buffer.concat([Buffer.alloc(5, 127), Buffer.from('look')])), 'UP again walks to the older entry');
ok(key(UP).length === 0, 'UP past the oldest entry does nothing');
const dn = key(DOWN); ok(dn.equals(Buffer.concat([Buffer.alloc(4, 127), Buffer.from('north')])), 'DOWN returns to the newer entry');
ok(key(DOWN).equals(Buffer.alloc(5, 127)), 'DOWN past newest clears the line');
ok(key('c'.charCodeAt(0), 1).equals(Buffer.from([3])), 'Ctrl+C -> 0x03');
ok(key(0x20ac).equals(Buffer.from('€')), 'non-ASCII key is UTF-8 encoded');

// ---- selection + copy ----
x.term_init();
feed('hello world\r\nsecond line   \r\n');
x.term_mouse(0, 0, 0); x.term_mouse(1, 4, 1); x.term_mouse(2, 4, 1);
let n2 = x.term_copy();
ok(Buffer.from(mem().slice(x.term_copy_addr(), x.term_copy_addr() + n2)).toString() === 'hello world\nsecon', 'drag selection copies text, trims trailing blanks, joins lines');
x.term_mouse(3, 8, 0);
n2 = x.term_copy();
ok(Buffer.from(mem().slice(x.term_copy_addr(), x.term_copy_addr() + n2)).toString() === 'world', 'double-click selects the word');
x.term_mouse(4, 0, 0); ok(x.term_has_sel() === 0 && x.term_copy() === 0, 'selection clears');

// ---- rendering ----
x.term_init();
ok(x.term_render() === 1, 'first render paints');
ok(x.term_render() === 0, 'second render with no change is skipped');
const px = (X, Y) => new DataView(x.memory.buffer).getUint32(x.term_fb_addr() + (Y * 800 + X) * 4, true);
ok(px(400, 300) >>> 24 === 255, 'framebuffer pixels are opaque');
feed('A'); x.term_set_cursor(0); x.term_render();
let lit = 0; for (let yy = 0; yy < 16; yy++) for (let xx = 0; xx < 8; xx++) if (px(xx, yy) !== px(400, 300)) lit++;
ok(lit > 10 && lit < 80, `glyph "A" paints a plausible number of pixels (${lit})`);
feed('\x1b[7mR'); x.term_render();
ok(px(8, 0) !== px(400, 300) || px(9, 5) !== px(400, 300), 'reverse video paints');

console.log(`\n${pass} passed, ${fail} failed`);
process.exit(fail ? 1 : 0);
