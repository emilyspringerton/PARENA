"""stdlib/netkit/term/nk_term.prn (the SSH terminal, compiled to wasm): VT behaviour checked against pyte (an independent
VT100/xterm emulator) on hand-written and random streams, plus exact checks of everything pyte does not model: raw key
encodings, the alternate screen, replies to the server, rendering and selection.

usage: test_term.py <nkterm.wasm>
"""
import os, random, sys
sys.path.insert(0, os.path.dirname(__file__))
import pyte
from nkharness import check, done
from wasmtime import Store, Module, Instance
import ctypes

class Term:
    IN, OUT, ST, FONT, GRID, FB, COPY = 0x20000, 0x30000, 0x31000, 0x32000, 0x100000, 0x300000, 0x4B0000
    COLS, ROWS = 100, 34
    def __init__(self, path):
        self.store = Store(); mod = Module.from_file(self.store.engine, path); assert not list(mod.imports)
        self.ex = Instance(self.store, mod, []).exports(self.store); self.mem = self.ex["memory"]; self.ex["term_init"](self.store)
    def call(self, name, *a): return self.ex[name](self.store, *a)
    def base(self): return ctypes.addressof(self.mem.data_ptr(self.store).contents)
    def write(self, addr, b): ctypes.memmove(self.base() + addr, bytes(b), len(b))
    def read(self, addr, n): return ctypes.string_at(self.base() + addr, n)
    def st(self, i): return int.from_bytes(self.read(self.ST + 4 * i, 4), "little", signed=True)
    def feed(self, data):
        data = bytes(data); out = b""
        for i in range(0, len(data), 60000):
            c = data[i:i + 60000]; self.write(self.IN, c); n = self.call("term_feed", len(c)); out += self.read(self.OUT, n)
        return out
    def key(self, code, mods=0): n = self.call("term_key", code, mods); return self.read(self.OUT, n)
    @staticmethod
    def glyph_char(g):
        return " " if g == 0 else chr(g + 32) if g < 95 else chr(9472 + g - 95) if g < 223 else chr(9600 + g - 223)
    def screen(self):
        """All 34 rows in one memory read (the grid is a ring: row r lives at absolute line top + r)."""
        top = self.st(2); rows = []
        for r in range(self.ROWS):
            a = self.GRID + (((top + r) % 1500) * self.COLS) * 12; raw = self.read(a, self.COLS * 12)
            rows.append("".join(self.glyph_char(int.from_bytes(raw[c * 12:c * 12 + 4], "little") & 0xFFFF) for c in range(self.COLS)).rstrip())
        return rows
    def line(self, r): return self.screen()[r]
    def cell(self, r, c):
        top = self.st(2); a = self.GRID + (((top + r) % 1500) * self.COLS + c) * 12
        w = int.from_bytes(self.read(a, 4), "little"); return (w & 0xFFFF, (w >> 16) & 255, int.from_bytes(self.read(a + 4, 4), "little"), int.from_bytes(self.read(a + 8, 4), "little"))

T = Term(sys.argv[1])
T.call("term_init")
def fresh():
    T.feed(b"\x1bc")                                     # RIS: the cleanest known state, and a test of RIS itself
def py(data):
    sc = pyte.Screen(100, 34); st = pyte.ByteStream(sc); st.feed(data); return [l.rstrip() for l in sc.display]

# ---- basic text and wrapping --------------------------------------------------------------------------------------------------------
fresh(); T.feed(b"hello\r\nworld")
check(T.line(0) == "hello" and T.line(1) == "world" and T.st(0) == 5 and T.st(1) == 1, "text, CR LF and cursor position")
fresh(); T.feed(b"x" * 100)
check(T.line(0) == "x" * 100 and T.st(1) == 0 and T.st(25) == 1, "100 characters fill the row; the wrap is deferred (cursor stays on row 0)")
T.feed(b"y"); check(T.line(1) == "y" and T.st(1) == 1, "the next character wraps to the following row")
fresh(); T.feed(b"\x1b[?7l" + b"z" * 105)
check(T.line(0) == "z" * 99 + "z" and T.line(1) == "", "autowrap off (?7l): extra characters overwrite the last column instead of wrapping")
T.feed(b"\x1b[?7h")

# ---- cursor movement and erase vs pyte --------------------------------------------------------------------------------------------------
cases = {
  "cup": b"\x1b[5;10Hxy\x1b[1;1Hab",
  "cuu/cud/cuf/cub": b"\x1b[10;10H\x1b[3Axx\x1b[2By\x1b[4Cz\x1b[6Dw",
  "clamp": b"\x1b[999;999Hq\x1b[0;0Hr\x1b[100Ds",
  "cha/vpa": b"abc\x1b[20Gd\x1b[12;1He\x1b[3dfg",
  "ed0": b"line1\r\nline2\r\nline3\x1b[2;3H\x1b[J",
  "ed1": b"line1\r\nline2\r\nline3\x1b[2;3H\x1b[1J",
  "ed2": b"line1\r\nline2\x1b[2J",
  "el0/1/2": b"0123456789\x1b[1;5H\x1b[K\r\n0123456789\x1b[2;5H\x1b[1K\r\n0123456789\x1b[3;5H\x1b[2K",
  "ich": b"abcdef\x1b[1;3H\x1b[2@XY",
  "dch": b"abcdef\x1b[1;3H\x1b[2P",
  "ech": b"abcdef\x1b[1;3H\x1b[3X",
  "il": b"1\r\n2\r\n3\r\n4\r\n5\x1b[2;1H\x1b[2L",
  "dl": b"1\r\n2\r\n3\r\n4\r\n5\x1b[2;1H\x1b[2M",
  "region": b"\x1b[1;1H" + b"".join(b"row%d\r\n" % i for i in range(30)) + b"\x1b[5;10r\x1b[10;1H\n\n\nbottom\x1b[5;1H\x1bMtop",
  "region+il": b"\x1b[1;34r" + b"".join(b"r%d\r\n" % i for i in range(12)) + b"\x1b[3;8r\x1b[4;1H\x1b[2L\x1b[5;1H\x1b[1M",
  "sgr": b"\x1b[1;31mred\x1b[0m plain \x1b[4munder\x1b[24m \x1b[7mrev\x1b[27m",
  "tabs": b"a\tb\t\tc",
  "bs": b"abc\b\bX\bY",
  "scroll": b"".join(b"line%02d\r\n" % i for i in range(60)),
  "save/restore": b"ab\x1b7\x1b[10;10Hcd\x1b8ef",
}
for name, data in cases.items():
    fresh(); T.feed(data); got = T.screen(); want = py(data)
    check(got == want, f"vs pyte: {name}" + ("" if got == want else f"  first diff row {next(i for i in range(34) if got[i] != want[i])}: {got[next(i for i in range(34) if got[i] != want[i])]!r} vs {want[next(i for i in range(34) if got[i] != want[i])]!r}"))

# pyte has no SU/SD and treats NEL as a bare line feed, so these two are checked against the VT100/xterm definitions directly
fresh(); T.feed(b"1\r\n2\r\n3\r\n4\r\n5\x1b[2S"); sc = T.screen()
check(sc[:4] == ["3", "4", "5", ""], "SU (ESC[2S) scrolls the region up two lines")
T.feed(b"\x1b[1T"); sc = T.screen(); check(sc[:5] == ["", "3", "4", "5", ""], "SD (ESC[1T) scrolls it back down one line")
fresh(); T.feed(b"ab\x1bEcd\x1bDef"); sc = T.screen()
check(sc[:3] == ["ab", "cd", "  ef"], "NEL (ESC E) = CR + LF; IND (ESC D) = LF keeping the column")

# ---- IL / DL / region edge cases, expectations derived by hand from the VT100/xterm definitions (pyte's sparse buffer gets some wrong:
#      e.g. it leaves a line in place when DL deletes more lines than hold content, and pops the saved cursor on every restore) ----
fresh(); T.feed(b"2\x1b[6M"); check(T.screen()[:3] == ["", "", ""] and (T.st(0), T.st(1)) == (0, 0), "DL deleting more lines than exist leaves blank lines and the cursor at column 0")
fresh(); T.feed(b"A\r\nB\r\nC\r\nD\x1b[2;1H\x1b[2M"); check(T.screen()[:4] == ["A", "D", "", ""], "DL 2 at row 2 pulls D up and blanks the end")
fresh(); T.feed(b"A\r\nB\r\nC\r\nD\x1b[2;1H\x1b[2L"); check(T.screen()[:6] == ["A", "", "", "B", "C", "D"], "IL 2 at row 2 pushes B, C, D down")
fresh(); T.feed(b"\x1b[3;6r"); T.feed(b"1\r\n2\r\n3\r\n4\r\n5\r\n6\r\n7\r\n8")
fresh(); T.feed(b"\x1b[1;1Ha\r\nb\r\nc\r\nd\r\ne\r\nf\x1b[2;4r\x1b[4;1H\n\ng")
check(T.screen()[:6] == ["a", "d", "", "g", "e", "f"], f"LF at the bottom margin scrolls only the region ({T.screen()[:6]})")
fresh(); T.feed(b"a\r\nb\r\nc\r\nd\r\ne\r\nf\x1b[2;4r\x1b[2;1H\x1bMX")
check(T.screen()[:6] == ["a", "X", "b", "c", "e", "f"], f"RI at the top margin scrolls the region DOWN, leaving rows outside it alone ({T.screen()[:6]})")
fresh(); T.feed(b"a\r\nb\r\nc\r\nd\r\ne\r\nf\x1b[2;4r\x1b[1;1H\x1bMX")
check(T.screen()[0] == "X" and T.screen()[1:6] == ["b", "c", "d", "e", "f"][:5], "RI above the region (row 1) does not scroll it")
fresh(); T.feed(b"x\x1b[3;5r"); check((T.st(0), T.st(1)) == (0, 0), "DECSTBM homes the cursor")
fresh(); T.feed(b"\x1b[5;3r\x1b[4;5r"); check((T.st(27), T.st(28)) == (3, 4), "a valid DECSTBM sets the margins; an inverted one (5;3) is ignored")
fresh(); T.feed(b"AB\x1b7\x1b[10;10HCD\x1b8\x1b8EF"); check(T.screen()[0] == "ABEF" and T.screen()[9].strip() == "CD", "DECRC restores the same saved cursor every time (xterm does not pop it)")
fresh(); T.feed(b"\x1b[3;1Hx\x1b[1;3r\x1b[3;1H\x1b[L"); check(T.screen()[2] == "", "IL inside the region inserts a blank line there")
fresh(); T.feed(b"a\r\nb\r\nc\r\nd\x1b[1;2r\x1b[4;1H\x1b[L"); check(T.screen()[:4] == ["a", "b", "c", "d"], "IL with the cursor OUTSIDE the region does nothing")

# ---- random streams, differential vs pyte -------------------------------------------------------------------------------------------------
# Only where pyte is a trustworthy oracle: group A = text, CR/LF/BS/TAB, absolute and relative moves, erase, ICH/DCH/ECH, SGR, RI/IND,
# one save slot. Group B = scroll regions, with the cursor kept inside the region (pyte clamps cursor-up/down to the region even from
# outside, which xterm does not) and no IL/DL.
R = random.Random(2026)
common = [b"\x1b[%d;%dH", b"\x1b[%dG", b"\x1b[%dd", b"\x1b[%dP", b"\x1b[%d@", b"\x1b[%dX", b"\x1b[K", b"\x1b[1K", b"\x1b[2K", b"\x1b[J", b"\x1b[1J",
          b"\x1b[m", b"\x1b[1m", b"\x1b[31m", b"\r", b"\n", b"\r\n", b"\r\n", b"\b", b"\t"]
group_a = common + [b"\x1b[%dA", b"\x1b[%dB", b"\x1b[%dC", b"\x1b[%dD", b"\x1b7", b"\x1bM", b"\x1bD"]
group_b = [b"\x1b[%dP", b"\x1b[%d@", b"\x1b[%dX", b"\x1b[K", b"\x1b[1K", b"\x1b[2K", b"\r", b"\n", b"\r\n", b"\r\n", b"\b", b"\x1bM"]
def rand_stream(seqset, n, region=None):
    # a CR LF at least every few items keeps lines far from the right edge: pyte's idea of the deferred-wrap state (it lets the cursor
    # sit AT column 100 and wraps on the next draw, even after a cursor move) differs from xterm's, which the hand-written wrap tests
    # above already pin down.
    out = b"\x1b[%d;%dr\x1b[%d;1H" % (region[0], region[1], region[0]) if region else b""
    for i in range(n):
        if i % 5 == 4: out += b"\r\n"
        if R.random() < 0.55: out += bytes(R.choice(b"abcdefghijklmnopqrstuvwxyz0123456789 .,:;-_") for _ in range(R.randint(1, 8)))
        else:
            s = R.choice(seqset); k = s.count(b"%d")
            out += s % tuple(R.randint(0, 12) for _ in range(k)) if k else s
    return out
for gname, gset, regioned in (("A (no regions)", group_a, False), ("B (scroll regions)", group_b, True)):
    bad = []
    for i in range(150):
        region = None
        if regioned: t = R.randint(2, 18); region = (t, t + R.randint(2, 12))
        data = rand_stream(gset, R.randint(20, 120), region); fresh(); T.feed(data)
        if T.screen() != py(data): bad.append(data)
    check(not bad, f"150 random VT streams, group {gname}: screens equal pyte's ({len(bad)} differ)")
    if bad:
        d = bad[0]; fresh(); T.feed(d); g = T.screen(); w = py(d)
        for r in range(34):
            if g[r] != w[r]: print("   first failing stream:", d[:300]); print("   row", r, repr(g[r]), "vs pyte", repr(w[r])); break

# ---- robustness: arbitrary garbage must never trap, escape the grid, or leave the cursor out of bounds -------------------------------------------------
bad = 0
for i in range(300):
    junk = bytes(R.randrange(256) for _ in range(R.randint(1, 400))) if i % 2 else b"".join(R.choice([b"\x1b[", b"\x1b", b"\x1b]", b";", b"?", b"99999", b"\x1b[%dr" % R.randint(0, 99), b"\x1b[%d;%dH" % (R.randint(0, 999), R.randint(0, 999)),
        b"\x1b[%dL" % R.randint(0, 9999), b"\x1b[%dM" % R.randint(0, 9999), b"\x1b[%d@" % R.randint(0, 9999), b"\x1b[%dP" % R.randint(0, 9999), b"\x1b[%dS" % R.randint(0, 9999),
        b"\x1b[?1049h", b"\x1b[?1049l", b"\x1b[?47h", b"\x1b[?47l", b"\n" * R.randint(1, 80), b"abc", "é".encode(), b"\xff", b"\xe2\x94"]) for _ in range(R.randint(1, 60)))
    try:
        fresh(); T.feed(junk)
        if not (0 <= T.st(0) < 100 and 0 <= T.st(1) < 34 and 0 <= T.st(27) <= T.st(28) < 34): bad += 1
    except Exception as e:
        bad += 1; print("   trap:", type(e).__name__, e)
check(bad == 0, "300 garbage / hostile streams (random bytes, truncated escapes, huge counts, region and alt-screen churn): no trap, cursor and margins always in bounds")

# ---- SGR colours land in the cells ---------------------------------------------------------------------------------------------------------
fresh(); T.feed(b"\x1b[31;44mA\x1b[0mB\x1b[1mC\x1b[38;5;196mD\x1b[48;2;1;2;3mE")
a, b_, c, d, e = [T.cell(0, i) for i in range(5)]
check(a[2] == 4936677 and a[3] == 15762352 - 15762352 + 16096083, "SGR 31;44 sets red foreground and blue background from the palette")
check(b_[2] == 15918054 and b_[3] == 1510157 and b_[1] == 0, "SGR 0 resets to the default colours and attributes")
check((c[1] & 1) == 1, "SGR 1 sets the bold flag")
check(d[2] == (255 | (0 << 8) | (0 << 16)), "256-colour foreground (index 196 = pure red)")
check(e[3] == (1 | (2 << 8) | (3 << 16)), "24-bit background colour")

# ---- DEC line drawing, UTF-8 box characters, placeholders --------------------------------------------------------------------------------------
fresh(); T.feed(b"\x1b(0lqqk\r\nx  x\r\nmqqj\x1b(Bok")
check(T.line(0) == "┌──┐" and T.line(1) == "│  │" and T.line(2) == "└──┘ok", "DEC line-drawing charset (ESC ( 0 ... ESC ( B) draws a box")
fresh(); T.feed("┌─┐ █ ▒ é".encode())
check(T.line(0).startswith("┌─┐ █ ▒ ") and T.line(0).endswith("?"), "UTF-8 box/block characters render; a character the font lacks (é) becomes a placeholder, not garbage")

# ---- alternate screen -------------------------------------------------------------------------------------------------------------------------------
fresh(); T.feed(b"main1\r\nmain2\x1b[2;3H"); T.feed(b"\x1b[?1049h\x1b[2JALT SCREEN\x1b[5;5Hfoo")
check(T.line(0) == "ALT SCREEN" and T.line(1) == "" and T.line(4) == "    foo" and T.call("term_alt_active") == 1, "?1049h switches to a blank alternate screen")
T.feed(b"\x1b[?1049l")
check(T.line(0) == "main1" and T.line(1) == "main2" and T.line(4) == "" and T.call("term_alt_active") == 0 and (T.st(0), T.st(1)) == (2, 1), "?1049l restores the main screen and the cursor exactly")
fresh(); T.feed(b"".join(b"L%d\r\n" % i for i in range(50))); t0 = T.st(2); T.feed(b"\x1b[?1049h" + b"".join(b"A%d\r\n" % i for i in range(50)) + b"\x1b[?1049l")
check(T.st(2) == t0, "scrolling inside the alternate screen adds nothing to the scrollback")

# ---- replies the server asks for -------------------------------------------------------------------------------------------------------------------
fresh(); T.feed(b"\x1b[5;9H"); check(T.feed(b"\x1b[6n") == b"\x1b[5;9R", "cursor position report: ESC[6n answers ESC[5;9R")
check(T.feed(b"\x1b[5n") == b"\x1b[0n", "device status: ESC[5n answers ESC[0n (ok)")
check(T.feed(b"\x1b[c") == b"\x1b[?1;2c", "primary device attributes are answered")
check(T.feed(b"abc") == b"", "ordinary text produces no reply")

# ---- modes reported to the page ----------------------------------------------------------------------------------------------------------------------
fresh(); check((T.call("term_bracketed"), T.call("term_app_cursor"), T.call("term_cursor_shown")) == (0, 0, 1), "defaults: no bracketed paste, normal cursor keys, cursor shown")
T.feed(b"\x1b[?2004h\x1b[?1h\x1b[?25l"); check((T.call("term_bracketed"), T.call("term_app_cursor"), T.call("term_cursor_shown")) == (1, 1, 0), "?2004h ?1h ?25l are tracked")
T.feed(b"\x1b[?2004l\x1b[?1l\x1b[?25h"); check((T.call("term_bracketed"), T.call("term_app_cursor"), T.call("term_cursor_shown")) == (0, 0, 1), "...and undone")

# ---- keys: the exact xterm sequences -------------------------------------------------------------------------------------------------------------------
fresh()
K = lambda c, m=0: T.key(c, m)
ENTER, BS, UP, DOWN, PGUP, PGDN, TAB, ESC, LEFT, RIGHT, HOME, END, DEL, INS, F1 = 1114113, 1114114, 1114115, 1114116, 1114117, 1114118, 1114119, 1114120, 1114121, 1114122, 1114123, 1114124, 1114125, 1114126, 1114127
CTRL, ALT, SHIFT = 1, 2, 4
check(K(ord("a")) == b"a" and K(ord("Z")) == b"Z" and K(ord(" ")) == b" ", "plain characters")
check(K(0xE9) == b"\xc3\xa9" and K(0x20AC) == "€".encode() and K(0x1F600) == "😀".encode(), "non-ASCII characters are sent as UTF-8")
check(K(ENTER) == b"\r" and K(BS) == b"\x7f" and K(TAB) == b"\t" and K(ESC) == b"\x1b", "Enter = CR, Backspace = DEL (0x7f), Tab, Esc")
check(K(TAB, SHIFT) == b"\x1b[Z", "Shift+Tab = ESC [ Z")
check([K(c) for c in (UP, DOWN, RIGHT, LEFT)] == [b"\x1b[A", b"\x1b[B", b"\x1b[C", b"\x1b[D"], "arrow keys (normal mode)")
check([K(c) for c in (HOME, END, DEL, INS, PGUP, PGDN)] == [b"\x1b[H", b"\x1b[F", b"\x1b[3~", b"\x1b[2~", b"\x1b[5~", b"\x1b[6~"], "Home, End, Delete, Insert, PgUp, PgDn")
T.feed(b"\x1b[?1h")
check([K(c) for c in (UP, DOWN, RIGHT, LEFT, HOME, END)] == [b"\x1bOA", b"\x1bOB", b"\x1bOC", b"\x1bOD", b"\x1bOH", b"\x1bOF"], "application cursor key mode (?1h): arrows and Home/End use ESC O")
T.feed(b"\x1b[?1l")
check(K(UP, CTRL) == b"\x1b[1;5A" and K(RIGHT, SHIFT) == b"\x1b[1;2C" and K(LEFT, ALT) == b"\x1b[1;3D" and K(UP, CTRL | SHIFT) == b"\x1b[1;6A", "modified arrows: ESC [ 1 ; <mod> <final>")
check(K(DEL, CTRL) == b"\x1b[3;5~" and K(PGUP, CTRL) == b"\x1b[5;5~", "modified tilde keys: ESC [ 3 ; 5 ~")
check([K(F1 + i) for i in range(12)] == [b"\x1bOP", b"\x1bOQ", b"\x1bOR", b"\x1bOS", b"\x1b[15~", b"\x1b[17~", b"\x1b[18~", b"\x1b[19~", b"\x1b[20~", b"\x1b[21~", b"\x1b[23~", b"\x1b[24~"], "F1..F12")
check(K(ord("c"), CTRL) == b"\x03" and K(ord("C"), CTRL) == b"\x03" and K(ord("a"), CTRL) == b"\x01" and K(ord("z"), CTRL) == b"\x1a" and K(ord("d"), CTRL) == b"\x04", "Ctrl+letter (c = 0x03, a = 0x01, z = 0x1a, d = 0x04), either case")
check(K(ord("["), CTRL) == b"\x1b" and K(ord("\\"), CTRL) == b"\x1c" and K(ord("]"), CTRL) == b"\x1d" and K(ord(" "), CTRL) == b"\x00" and K(ord("@"), CTRL) == b"\x00" and K(ord("?"), CTRL) == b"\x7f", "Ctrl+[ \\ ] space @ ?")
check(K(ord("x"), ALT) == b"\x1bx" and K(ord("f"), ALT | CTRL) == b"\x1b\x06", "Alt+key = ESC prefix (Alt+Ctrl+F = ESC ACK)")
T.feed(b"".join(b"s%d\r\n" % i for i in range(80))); v0 = T.call("term_view")
check(K(PGUP, SHIFT) == b"" and T.call("term_view") > v0, "Shift+PgUp scrolls the local history and sends nothing")
check(K(PGDN, SHIFT) == b"" and T.call("term_view") == v0, "Shift+PgDn scrolls back")
T.call("term_scroll", 5); K(ord("a")); check(T.call("term_view") == 0, "typing returns the view to the live screen")

# ---- rendering ------------------------------------------------------------------------------------------------------------------------------------------
fresh(); T.feed(b"A"); r1 = T.call("term_render"); r2 = T.call("term_render")
check(r1 == 1 and r2 == 0, "term_render paints when something changed and reports 0 when nothing did")
g = T.cell(0, 0)[0]; bits = [T.read(T.FONT + g * 16 + y, 1)[0] for y in range(16)]
fg, bg = 15918054, 1510157
ok = True
for y in range(16):
    for x in range(8):
        px = int.from_bytes(T.read(T.FB + ((y * 800) + (x + 8 * 1)) * 4, 4), "little")    # cell (0,1): the cursor is at col 1, 'A' is at col 0
    for x in range(8):
        px = int.from_bytes(T.read(T.FB + ((y * 800) + x) * 4, 4), "little"); want = 0xFF000000 | (fg if (bits[y] >> (7 - x)) & 1 else bg)
        ok &= (px == want)
check(ok, "the 'A' cell is painted exactly from the font bitmap, opaque, in the foreground/background colours")
cur = int.from_bytes(T.read(T.FB + ((0 * 800) + 8) * 4, 4), "little")
check(cur == (0xFF000000 | 4965104), "the cursor cell (col 1) is painted in the cursor colour")
T.call("term_set_cursor", 0); T.call("term_render"); check(int.from_bytes(T.read(T.FB + 8 * 4, 4), "little") == (0xFF000000 | bg), "hiding the cursor repaints that cell normally")

# ---- selection / copy ----------------------------------------------------------------------------------------------------------------------------------------
fresh(); T.feed(b"first line\r\nsecond line\r\nthird")
T.call("term_mouse", 0, 6, 0); T.call("term_mouse", 1, 5, 1); T.call("term_mouse", 2, 5, 1)
n = T.call("term_copy"); check(T.read(T.COPY, n).decode() == "line\nsecond", "drag-select across two rows copies the text (trailing blanks trimmed)")
T.call("term_mouse", 3, 2, 2); n = T.call("term_copy"); check(T.read(T.COPY, n).decode() == "third", "double-click selects the word")
T.call("term_mouse", 4, 0, 0); check(T.call("term_copy") == 0, "clearing the selection copies nothing")

# ---- a realistic bash session ----------------------------------------------------------------------------------------------------------------------------
fresh()
stream = (b"ubuntu@pi:~$ ls\r\n\x1b[0m\x1b[01;34mDocuments\x1b[0m  \x1b[01;32mscript.sh\x1b[0m  notes.txt\r\nubuntu@pi:~$ echo helo"
          b"\x08\x1b[Klo wor\x08\x08\x08\x1b[P\x1b[1@x\x1b[3C")
fresh(); T.feed(stream); want = py(stream)
check(T.screen()[:3] == want[:3], "a realistic bash transcript (colours, backspace + EL, DCH/ICH line editing) matches pyte")
done()
