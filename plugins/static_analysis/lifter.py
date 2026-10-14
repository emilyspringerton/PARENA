"""Headless binary loading and multi-tier IR lifting (LLIL -> MLIL -> HLIL).

Pure Python, no third-party dependencies.  Supports ELF64 and PE32+ images (and
raw byte blobs) containing x86-64 code.  The instruction decoder covers the
integer subset gcc/clang emit for ordinary C; anything outside it is surfaced
explicitly (``LUnimpl`` / ``LTrap``) rather than silently mis-lifted.

Tiers
-----
LLIL  one statement list per machine instruction, normalised operations, with
      flag writes, stack-pointer mutation and return-address pushes all explicit.
MLIL  basic-block CFG over named variables: registers and frame slots become
      variables, flags are folded into comparison conditions, frame bookkeeping
      (push rbp / mov rbp,rsp / leave) is absorbed into slot naming.
HLIL  structured pseudo-code (built in ``decompiler.py``).

Public entry points: ``Lifter.open(path)``, ``Lifter.from_bytes(code, base)``,
``Function.llil / .mlil / .ssa / .hlil``.
"""
from __future__ import annotations

import struct
from dataclasses import dataclass, field
from functools import cached_property
from typing import Callable, Dict, Iterable, List, Optional, Tuple

MASK64 = (1 << 64) - 1


# ---------------------------------------------------------------------------
# Expressions (shared by LLIL and MLIL)
# ---------------------------------------------------------------------------

class Expr:
    def children(self) -> Tuple["Expr", ...]:
        return ()

    def rebuild(self, kids: Tuple["Expr", ...]) -> "Expr":
        return self

    def map(self, fn: Callable[["Expr"], "Expr"]) -> "Expr":
        """Rebuild this node with ``fn`` applied to every direct child."""
        kids = self.children()
        return self.rebuild(tuple(fn(k) for k in kids)) if kids else self

    def __str__(self) -> str:
        return fmt_expr(self)


@dataclass(frozen=True)
class Const(Expr):
    value: int

    def __post_init__(self):
        object.__setattr__(self, "value", self.value & MASK64)


@dataclass(frozen=True)
class Reg(Expr):
    """LLIL architectural register (always the full 64-bit family name)."""
    name: str


@dataclass(frozen=True)
class Flag(Expr):
    name: str


@dataclass(frozen=True)
class Cond(Expr):
    """LLIL condition-code predicate (x86 cc name: e, ne, l, ge, b, ...)."""
    cc: str


@dataclass(frozen=True)
class Var(Expr):
    """MLIL variable; ``version`` is None outside SSA form."""
    name: str
    version: Optional[int] = None

    @property
    def ssa_name(self) -> str:
        return self.name if self.version is None else f"{self.name}_{self.version}"


@dataclass(frozen=True)
class AddrOf(Expr):
    var: Var


@dataclass(frozen=True)
class Load(Expr):
    addr: Expr
    size: int

    def children(self):
        return (self.addr,)

    def rebuild(self, k):
        return Load(k[0], self.size)


@dataclass(frozen=True)
class Bin(Expr):
    op: str
    l: Expr
    r: Expr

    def children(self):
        return (self.l, self.r)

    def rebuild(self, k):
        return Bin(self.op, k[0], k[1])


@dataclass(frozen=True)
class Un(Expr):
    op: str  # neg, not, !, parity
    e: Expr

    def children(self):
        return (self.e,)

    def rebuild(self, k):
        return Un(self.op, k[0])


@dataclass(frozen=True)
class Ext(Expr):
    kind: str  # zx | sx
    e: Expr
    size: int  # source width in bytes

    def children(self):
        return (self.e,)

    def rebuild(self, k):
        return Ext(self.kind, k[0], self.size)


@dataclass(frozen=True)
class Tern(Expr):
    cond: Expr
    t: Expr
    f: Expr

    def children(self):
        return (self.cond, self.t, self.f)

    def rebuild(self, k):
        return Tern(k[0], k[1], k[2])


@dataclass(frozen=True)
class Undef(Expr):
    """Architecturally undefined value (e.g. cf after a shift)."""


CMP_OPS = {"==", "!=", "<s", "<=s", ">s", ">=s", "<u", "<=u", ">u", ">=u"}
_NEG_CMP = {"==": "!=", "!=": "==", "<s": ">=s", ">=s": "<s", "<=s": ">s", ">s": "<=s",
            "<u": ">=u", ">=u": "<u", "<=u": ">u", ">u": "<=u"}
_PREC = {"*": 6, "/s": 6, "/u": 6, "%s": 6, "%u": 6, "+": 5, "-": 5, "<<": 4, ">>": 4, "s>>": 4,
         "<s": 3, "<=s": 3, ">s": 3, ">=s": 3, "<u": 3, "<=u": 3, ">u": 3, ">=u": 3,
         "==": 2, "!=": 2, "&": 1.6, "^": 1.4, "|": 1.2}
_OPSTR = {"<s": "<", "<=s": "<=", ">s": ">", ">=s": ">=", "<u": "u<", "<=u": "u<=", ">u": "u>",
          ">=u": "u>=", "/s": "/", "/u": "u/", "%s": "%", "%u": "u%", "s>>": "s>>"}
_TY = {1: "uint8_t", 2: "uint16_t", 4: "uint32_t", 8: "uint64_t"}
_STY = {1: "int8_t", 2: "int16_t", 4: "int32_t", 8: "int64_t"}


def fmt_const(v: int) -> str:
    if v >= 1 << 63:
        return f"-{(1 << 64) - v:#x}"
    return str(v) if v < 10 else f"{v:#x}"


def fmt_expr(e: Expr, parent_prec: float = 0) -> str:
    if isinstance(e, Const):
        return fmt_const(e.value)
    if isinstance(e, Reg):
        return e.name
    if isinstance(e, Flag):
        return f"flags.{e.name}"
    if isinstance(e, Cond):
        return f"cond({e.cc})"
    if isinstance(e, Var):
        return e.name if e.version is None else f"{e.name}#{e.version}"
    if isinstance(e, AddrOf):
        return f"&{fmt_expr(e.var)}"
    if isinstance(e, Undef):
        return "undefined"
    if isinstance(e, Load):
        return f"*({_TY.get(e.size, f'u{e.size * 8}')}*){fmt_expr(e.addr, 9)}"
    if isinstance(e, Ext):
        ty = (_TY if e.kind == "zx" else _STY).get(e.size, f"{e.kind}{e.size * 8}")
        return f"({ty}){fmt_expr(e.e, 9)}"
    if isinstance(e, Un):
        if e.op == "parity":
            return f"parity({fmt_expr(e.e)})"
        return f"{ {'neg': '-', 'not': '~', '!': '!'}[e.op] }{fmt_expr(e.e, 9)}"
    if isinstance(e, Tern):
        return f"({fmt_expr(e.cond)} ? {fmt_expr(e.t)} : {fmt_expr(e.f)})"
    if isinstance(e, Bin):
        if e.op.startswith(("sub_of", "add_of")):
            return f"{e.op}({fmt_expr(e.l)}, {fmt_expr(e.r)})"
        op, prec = e.op, _PREC.get(e.op, 0)
        r = e.r
        if op == "+" and isinstance(r, Const) and r.value >= 1 << 63:
            op, r = "-", Const(-r.value)
        s = f"{fmt_expr(e.l, prec)} {_OPSTR.get(op, op)} {fmt_expr(r, prec + 0.1)}"
        return f"({s})" if prec < parent_prec else s
    if hasattr(e, "fmt"):
        return e.fmt(parent_prec)
    return repr(e)


# ---------------------------------------------------------------------------
# Constant folding / algebraic simplification
# ---------------------------------------------------------------------------

def _signed(v: int, bits: int = 64) -> int:
    v &= (1 << bits) - 1
    return v - (1 << bits) if v >> (bits - 1) else v


def _is_cmp(e: Expr) -> bool:
    return (isinstance(e, Bin) and e.op in CMP_OPS) or (isinstance(e, Un) and e.op == "!")


def _eval_bin(op: str, a: int, b: int) -> Optional[int]:
    if op == "+": return (a + b) & MASK64
    if op == "-": return (a - b) & MASK64
    if op == "*": return (a * b) & MASK64
    if op == "&": return a & b
    if op == "|": return a | b
    if op == "^": return a ^ b
    if op == "<<": return (a << (b & 63)) & MASK64
    if op == ">>": return a >> (b & 63)
    if op == "s>>": return (_signed(a) >> (b & 63)) & MASK64
    if op == "==": return int(a == b)
    if op == "!=": return int(a != b)
    if op == "<u": return int(a < b)
    if op == "<=u": return int(a <= b)
    if op == ">u": return int(a > b)
    if op == ">=u": return int(a >= b)
    sa, sb = _signed(a), _signed(b)
    if op == "<s": return int(sa < sb)
    if op == "<=s": return int(sa <= sb)
    if op == ">s": return int(sa > sb)
    if op == ">=s": return int(sa >= sb)
    if op in ("/u", "%u"):
        if b == 0: return None
        return a // b if op == "/u" else a % b
    if op in ("/s", "%s"):
        if sb == 0: return None
        q = abs(sa) // abs(sb)
        if (sa < 0) != (sb < 0): q = -q
        return (q if op == "/s" else sa - q * sb) & MASK64
    if op.startswith(("sub_of", "add_of")):
        n = int(op[6:]) if op.startswith("sub_of") else int(op[6:])
        bits = n * 8
        xa, xb = _signed(a, bits), _signed(b, bits)
        r = xa - xb if op.startswith("sub_of") else xa + xb
        return int(not (-(1 << (bits - 1)) <= r < (1 << (bits - 1))))
    return None


def _has_undef(e: Expr) -> bool:
    return isinstance(e, Undef) or any(_has_undef(c) for c in e.children())


def fold(e: Expr) -> Expr:
    """Bottom-up constant folding with a few algebraic identities."""
    e = e.map(fold)
    if isinstance(e, Bin):
        l, r, op = e.l, e.r, e.op
        if isinstance(l, Const) and isinstance(r, Const):
            v = _eval_bin(op, l.value, r.value)
            if v is not None:
                return Const(v)
        if op == "-" and isinstance(r, Const):          # normalise x - c -> x + (-c)
            return fold(Bin("+", l, Const(-r.value)))
        if op in ("+", "*", "&", "|", "^") and isinstance(l, Const) and not isinstance(r, Const):
            l, r = r, l
            e = Bin(op, l, r)
        if op == "+" and isinstance(r, Const):
            if r.value == 0: return l
            if isinstance(l, Bin) and l.op == "+" and isinstance(l.r, Const):
                return fold(Bin("+", l.l, Const(l.r.value + r.value)))
        if op in ("|", "^", "<<", ">>", "s>>") and isinstance(r, Const) and r.value == 0: return l
        if op == "*" and isinstance(r, Const):
            if r.value == 1: return l
            if r.value == 0: return Const(0)
        if op == "&" and isinstance(r, Const):
            if r.value == 0: return Const(0)
            if r.value == MASK64: return l
        if l == r and not isinstance(l, Load) and not _has_undef(l):
            if op in ("^", "-"): return Const(0)
            if op in ("&", "|"): return l
            if op in ("==", "<=s", ">=s", "<=u", ">=u"): return Const(1)
            if op in ("!=", "<s", ">s", "<u", ">u"): return Const(0)
        if op in ("==", "!=") and isinstance(r, Const) and r.value == 0 and _is_cmp(l):
            return l if op == "!=" else fold(Un("!", l))
        return e
    if isinstance(e, Un):
        x = e.e
        if isinstance(x, Const):
            if e.op == "neg": return Const(-x.value)
            if e.op == "not": return Const(~x.value)
            if e.op == "!": return Const(int(x.value == 0))
            if e.op == "parity": return Const(int(bin(x.value & 0xFF).count("1") % 2 == 0))
        if e.op == "!":
            if isinstance(x, Bin) and x.op in _NEG_CMP: return Bin(_NEG_CMP[x.op], x.l, x.r)
            if isinstance(x, Un) and x.op == "!" and _is_cmp(x.e): return x.e
        if e.op in ("neg", "not") and isinstance(x, Un) and x.op == e.op: return x.e
        return e
    if isinstance(e, Ext):
        x = e.e
        if e.size >= 8: return x
        if isinstance(x, Const):
            m = (1 << (e.size * 8)) - 1
            if e.kind == "zx": return Const(x.value & m)
            return Const(_signed(x.value, e.size * 8))
        if e.kind == "zx" and _is_cmp(x): return x
        if isinstance(x, Ext) and x.kind == "zx":
            if e.kind == "zx": return Ext("zx", x.e, min(e.size, x.size))
            if e.size > x.size: return x   # zero-extended value is already non-negative
            return Ext("sx", x.e, e.size)
        return e
    if isinstance(e, Tern):
        if isinstance(e.cond, Const): return e.t if e.cond.value else e.f
        if e.t == e.f: return e.t
    return e


# ---------------------------------------------------------------------------
# LLIL
# ---------------------------------------------------------------------------

@dataclass
class LInsn:
    addr: int = 0

    def __str__(self):  # pragma: no cover - overridden
        return type(self).__name__


@dataclass
class LSetReg(LInsn):
    reg: str = ""
    src: Expr = None
    def __str__(self): return f"{self.reg} = {self.src}"


@dataclass
class LSetFlag(LInsn):
    flag: str = ""
    src: Expr = None
    def __str__(self): return f"flags.{self.flag} = {self.src}"


@dataclass
class LStore(LInsn):
    mem_addr: Expr = None
    src: Expr = None
    size: int = 8
    retaddr: bool = False  # push of the return address performed by `call`
    def __str__(self): return f"[{self.mem_addr}].{self.size} = {self.src}"


@dataclass
class LCall(LInsn):
    target: Expr = None
    def __str__(self): return f"call({self.target})"


@dataclass
class LJump(LInsn):
    target: Expr = None
    def __str__(self): return f"jump({self.target})"


@dataclass
class LIf(LInsn):
    cond: Cond = None
    true_addr: int = 0
    false_addr: int = 0
    def __str__(self): return f"if ({self.cond}) goto {self.true_addr:#x} else {self.false_addr:#x}"


@dataclass
class LRet(LInsn):
    target: Expr = None
    def __str__(self): return f"return {self.target}"


@dataclass
class LTrap(LInsn):
    reason: str = ""
    def __str__(self): return f"trap({self.reason})"


@dataclass
class LNop(LInsn):
    def __str__(self): return "nop"


@dataclass
class LUnimpl(LInsn):
    text: str = ""
    def __str__(self): return f"unimplemented({self.text})"


LLIL_TERMINATORS = (LJump, LIf, LRet, LTrap)


@dataclass
class LLILBlock:
    start: int
    insns: List[LInsn] = field(default_factory=list)
    succs: List[int] = field(default_factory=list)  # successor block start addrs


@dataclass
class LLILFunction:
    start: int
    insns: List[LInsn]               # in address order
    blocks: List[LLILBlock]          # blocks[0] is the entry

    def __str__(self):
        return "\n".join(f"{i.addr:#x}: {i}" for i in self.insns)


# ---------------------------------------------------------------------------
# x86-64 decoder
# ---------------------------------------------------------------------------

REG64 = ["rax", "rcx", "rdx", "rbx", "rsp", "rbp", "rsi", "rdi",
         "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15"]
CC = ["o", "no", "b", "ae", "e", "ne", "be", "a", "s", "ns", "p", "np", "l", "ge", "le", "g"]
ALU = ["add", "or", "adc", "sbb", "and", "sub", "xor", "cmp"]
SYSV_ARGS = ["rdi", "rsi", "rdx", "rcx", "r8", "r9"]
CALLER_SAVED = ["rax", "rcx", "rdx", "rsi", "rdi", "r8", "r9", "r10", "r11"]


@dataclass
class Opnd:
    kind: str                   # reg | imm | mem
    size: int = 8
    reg: str = ""
    high8: bool = False         # ah/ch/dh/bh
    imm: int = 0
    base: Optional[str] = None
    index: Optional[str] = None
    scale: int = 1
    disp: int = 0
    seg: Optional[str] = None


@dataclass
class Insn:
    addr: int
    size: int
    mnem: str
    ops: List[Opnd] = field(default_factory=list)
    opsize: int = 4
    cc: str = ""
    target: Optional[int] = None  # direct branch target

    @property
    def next_addr(self) -> int:
        return self.addr + self.size


class DecodeError(Exception):
    pass


def decode(code: bytes, off: int, addr: int) -> Insn:
    """Decode one x86-64 instruction at code[off:]; raises DecodeError."""
    i = off
    n = len(code)

    def u8():
        nonlocal i
        if i >= n: raise DecodeError("truncated")
        v = code[i]; i += 1
        return v

    def imm(nbytes, signed=True):
        nonlocal i
        if i + nbytes > n: raise DecodeError("truncated")
        v = int.from_bytes(code[i:i + nbytes], "little", signed=signed)
        i += nbytes
        return v

    opsz16 = False; rep = None; seg = None; rex = 0
    while True:
        b = u8()
        if b == 0x66: opsz16 = True
        elif b in (0xF2, 0xF3): rep = b
        elif b in (0x64, 0x65): seg = "fsbase" if b == 0x64 else "gsbase"
        elif b in (0x2E, 0x3E, 0xF0): pass
        else: break
    if 0x40 <= b <= 0x4F:
        rex = b; b = u8()
    W, R, X, B = bool(rex & 8), bool(rex & 4) << 3, bool(rex & 2) << 3, bool(rex & 1) << 3
    osz = 8 if W else (2 if opsz16 else 4)

    def mkreg(num, size) -> Opnd:
        if size == 1 and not rex and 4 <= num <= 7:
            return Opnd("reg", 1, REG64[num - 4], high8=True)
        return Opnd("reg", size, REG64[num])

    rip_fix: List[Opnd] = []

    def modrm(size):
        """returns (regfield, rm operand)"""
        nonlocal i
        m = u8()
        mod, reg, rm = m >> 6, ((m >> 3) & 7), m & 7
        regnum = reg | R
        if mod == 3:
            return regnum, mkreg(rm | B, size)
        o = Opnd("mem", size, seg=seg)
        if rm == 4:
            sib = u8()
            sc, idx, bs = 1 << (sib >> 6), ((sib >> 3) & 7) | X, (sib & 7)
            o.scale = sc
            o.index = None if idx == 4 else REG64[idx]
            if bs == 5 and mod == 0:
                o.base = None; o.disp = imm(4)
            else:
                o.base = REG64[bs | B]
        elif rm == 5 and mod == 0:
            o.base = "rip"; o.disp = imm(4); rip_fix.append(o)
        else:
            o.base = REG64[rm | B]
        if mod == 1: o.disp = imm(1)
        elif mod == 2: o.disp = imm(4)
        return regnum, o

    def immop(nbytes, size):
        v = imm(nbytes)
        return Opnd("imm", size, imm=v & ((1 << (size * 8)) - 1) if size < 8 else v & MASK64)

    def mk(mnem, ops, size=None, **kw):
        ins = Insn(addr, i - off, mnem, ops, size or osz, **kw)
        for o in rip_fix: o.base = None; o.disp += addr + ins.size
        return ins

    def alu_name(idx): return ALU[idx]

    op = b
    if op < 0x40 and (op & 7) < 6 and op not in (0x0F,):
        name = alu_name(op >> 3)
        form = op & 7
        if form in (0, 1, 2, 3):
            sz = 1 if form in (0, 2) else osz
            reg, rm = modrm(sz)
            r = mkreg(reg, sz)
            ops = [rm, r] if form in (0, 1) else [r, rm]
            return mk(name, ops, sz)
        sz = 1 if form == 4 else osz
        ops = [mkreg(0, sz), immop(1 if sz == 1 else (2 if sz == 2 else 4), sz)]
        return mk(name, ops, sz)
    if 0x50 <= op <= 0x57: return mk("push", [Opnd("reg", 8, REG64[(op - 0x50) | B])], 8)
    if 0x58 <= op <= 0x5F: return mk("pop", [Opnd("reg", 8, REG64[(op - 0x58) | B])], 8)
    if op == 0x63:
        reg, rm = modrm(4); return mk("movsxd", [mkreg(reg, osz), rm], osz)
    if op == 0x68: return mk("push", [Opnd("imm", 8, imm=imm(4) & MASK64)], 8)
    if op == 0x6A: return mk("push", [Opnd("imm", 8, imm=imm(1) & MASK64)], 8)
    if op in (0x69, 0x6B):
        reg, rm = modrm(osz)
        v = imm(1 if op == 0x6B else (2 if osz == 2 else 4))
        return mk("imul", [mkreg(reg, osz), rm, Opnd("imm", osz, imm=v & MASK64)], osz)
    if 0x70 <= op <= 0x7F:
        d = imm(1); ins = mk("jcc", [], cc=CC[op - 0x70]); ins.target = addr + ins.size + d; return ins
    if op in (0x80, 0x81, 0x83):
        sz = 1 if op == 0x80 else osz
        sub, rm = modrm(sz)
        nb = 1 if op in (0x80, 0x83) else (2 if sz == 2 else 4)
        v = imm(nb)
        mask = (1 << (sz * 8)) - 1 if sz < 8 else MASK64
        return mk(alu_name(sub & 7), [rm, Opnd("imm", sz, imm=v & mask)], sz)
    if op in (0x84, 0x85):
        sz = 1 if op == 0x84 else osz
        reg, rm = modrm(sz); return mk("test", [rm, mkreg(reg, sz)], sz)
    if op in (0x88, 0x89, 0x8A, 0x8B):
        sz = 1 if op in (0x88, 0x8A) else osz
        reg, rm = modrm(sz)
        r = mkreg(reg, sz)
        return mk("mov", [rm, r] if op in (0x88, 0x89) else [r, rm], sz)
    if op == 0x8D:
        reg, rm = modrm(osz); return mk("lea", [mkreg(reg, osz), rm], osz)
    if op == 0x90: return mk("nop", [])
    if op == 0x98: return mk("cdqe" if W else ("cbw" if opsz16 else "cwde"), [], osz)
    if op == 0x99: return mk("cqo" if W else ("cwd" if opsz16 else "cdq"), [], osz)
    if op in (0xA8, 0xA9):
        sz = 1 if op == 0xA8 else osz
        return mk("test", [mkreg(0, sz), immop(1 if sz == 1 else (2 if sz == 2 else 4), sz)], sz)
    if 0xB0 <= op <= 0xB7:
        return mk("mov", [mkreg((op - 0xB0) | B, 1), immop(1, 1)], 1)
    if 0xB8 <= op <= 0xBF:
        r = mkreg((op - 0xB8) | B, osz)
        if W: v = Opnd("imm", 8, imm=imm(8, False))
        else: v = immop(2 if osz == 2 else 4, osz)
        return mk("mov", [r, v], osz)
    if op in (0xC0, 0xC1, 0xD0, 0xD1, 0xD2, 0xD3):
        sz = 1 if op in (0xC0, 0xD0, 0xD2) else osz
        sub, rm = modrm(sz)
        names = {0: "rol", 1: "ror", 4: "shl", 5: "shr", 6: "shl", 7: "sar"}
        nm = names.get(sub & 7, "shift?")
        if op in (0xC0, 0xC1): cnt = Opnd("imm", 1, imm=imm(1, False))
        elif op in (0xD0, 0xD1): cnt = Opnd("imm", 1, imm=1)
        else: cnt = Opnd("reg", 1, "rcx")
        return mk(nm, [rm, cnt], sz)
    if op == 0xC3: return mk("ret", [])
    if op == 0xC9: return mk("leave", [])
    if op in (0xC6, 0xC7):
        sz = 1 if op == 0xC6 else osz
        sub, rm = modrm(sz)
        if sub & 7: raise DecodeError("c6/c7 /n")
        nb = 1 if sz == 1 else (2 if sz == 2 else 4)
        v = imm(nb)
        mask = (1 << (sz * 8)) - 1 if sz < 8 else MASK64
        return mk("mov", [rm, Opnd("imm", sz, imm=v & mask)], sz)
    if op == 0xCC: return mk("int3", [])
    if op == 0xE8:
        d = imm(4); ins = mk("call", []); ins.target = (addr + ins.size + d) & MASK64; return ins
    if op == 0xE9:
        d = imm(4); ins = mk("jmp", []); ins.target = (addr + ins.size + d) & MASK64; return ins
    if op == 0xEB:
        d = imm(1); ins = mk("jmp", []); ins.target = addr + ins.size + d; return ins
    if op == 0xF4: return mk("hlt", [])
    if op in (0xF6, 0xF7):
        sz = 1 if op == 0xF6 else osz
        sub, rm = modrm(sz)
        sub &= 7
        if sub in (0, 1):
            v = imm(1 if sz == 1 else (2 if sz == 2 else 4))
            mask = (1 << (sz * 8)) - 1 if sz < 8 else MASK64
            return mk("test", [rm, Opnd("imm", sz, imm=v & mask)], sz)
        nm = {2: "not", 3: "neg", 4: "mul", 5: "imul1", 6: "div", 7: "idiv"}[sub]
        return mk(nm, [rm], sz)
    if op in (0xFE, 0xFF):
        sz = 1 if op == 0xFE else osz
        sub, rm = modrm(sz)
        sub &= 7
        if sub == 0: return mk("inc", [rm], sz)
        if sub == 1: return mk("dec", [rm], sz)
        if op == 0xFF:
            rm.size = 8
            if sub == 2: return mk("call", [rm], 8)
            if sub == 4: return mk("jmp", [rm], 8)
            if sub == 6: return mk("push", [rm], 8)
        raise DecodeError("ff /n")
    if op == 0x0F:
        o2 = u8()
        if o2 == 0x05: return mk("syscall", [])
        if o2 == 0x0B: return mk("ud2", [])
        if o2 == 0x1E and rep == 0xF3:
            u8(); return mk("nop", [])  # endbr64
        if o2 == 0x1F:
            _, rm = modrm(osz); return mk("nop", [])
        if 0x40 <= o2 <= 0x4F:
            reg, rm = modrm(osz); return mk("cmovcc", [mkreg(reg, osz), rm], osz, cc=CC[o2 - 0x40])
        if 0x80 <= o2 <= 0x8F:
            d = imm(4); ins = mk("jcc", [], cc=CC[o2 - 0x80]); ins.target = addr + ins.size + d; return ins
        if 0x90 <= o2 <= 0x9F:
            _, rm = modrm(1); return mk("setcc", [rm], 1, cc=CC[o2 - 0x90])
        if o2 == 0xAF:
            reg, rm = modrm(osz); return mk("imul", [mkreg(reg, osz), rm], osz)
        if o2 in (0xB6, 0xB7, 0xBE, 0xBF):
            ssz = 1 if o2 in (0xB6, 0xBE) else 2
            reg, rm = modrm(ssz)
            return mk("movzx" if o2 < 0xB8 else "movsx", [mkreg(reg, osz), rm], osz)
        raise DecodeError(f"0f {o2:02x}")
    raise DecodeError(f"opcode {op:02x}")


# ---------------------------------------------------------------------------
# x86-64 -> LLIL
# ---------------------------------------------------------------------------

def _mask(size: int) -> int:
    return MASK64 if size >= 8 else (1 << (size * 8)) - 1


def _sx(e: Expr, size: int) -> Expr:
    return e if size >= 8 else Ext("sx", e, size)


def _zx(e: Expr, size: int) -> Expr:
    return e if size >= 8 else Ext("zx", e, size)


def _addr_expr(o: Opnd) -> Expr:
    e: Optional[Expr] = None
    if o.base: e = Reg(o.base)
    if o.index:
        ix = Reg(o.index)
        ix = ix if o.scale == 1 else Bin("*", ix, Const(o.scale))
        e = ix if e is None else Bin("+", e, ix)
    if o.seg:
        s = Reg(o.seg)
        e = s if e is None else Bin("+", s, e)
    if o.disp or e is None:
        e = Const(o.disp) if e is None else Bin("+", e, Const(o.disp))
    return fold(e)


def _read(o: Opnd, size: Optional[int] = None) -> Expr:
    size = size or o.size
    if o.kind == "imm":
        return Const(o.imm & _mask(size)) if size < 8 else Const(o.imm)
    if o.kind == "reg":
        full: Expr = Reg(o.reg)
        if o.high8:
            return Ext("zx", Bin(">>", full, Const(8)), 1)
        return _zx(full, size)
    return Load(_addr_expr(o), size)


def _write(o: Opnd, val: Expr, size: Optional[int] = None) -> List[LInsn]:
    size = size or o.size
    if o.kind == "mem":
        return [LStore(0, _addr_expr(o), val, size)]
    full = Reg(o.reg)
    if size == 8: return [LSetReg(0, o.reg, val)]
    if size == 4: return [LSetReg(0, o.reg, _zx(val, 4))]      # 32-bit writes zero-extend
    if o.high8:
        merged = Bin("|", Bin("&", full, Const(~0xFF00)), Bin("<<", _zx(val, 1), Const(8)))
    else:
        m = _mask(size)
        merged = Bin("|", Bin("&", full, Const(~m)), _zx(val, size))
    return [LSetReg(0, o.reg, merged)]


def _flags_logic(res: Expr, size: int) -> List[LInsn]:
    return [LSetFlag(0, "cf", Const(0)), LSetFlag(0, "of", Const(0)),
            LSetFlag(0, "zf", Bin("==", _zx(res, size), Const(0))),
            LSetFlag(0, "sf", Bin("<s", _sx(res, size), Const(0))),
            LSetFlag(0, "pf", Un("parity", res))]


def _zsp(res: Expr, size: int) -> List[LInsn]:
    return [LSetFlag(0, "zf", Bin("==", _zx(res, size), Const(0))),
            LSetFlag(0, "sf", Bin("<s", _sx(res, size), Const(0))),
            LSetFlag(0, "pf", Un("parity", res))]


def lift_insn(ins: Insn, binary: Optional["BinaryView"] = None) -> List[LInsn]:
    """Lower one machine instruction to LLIL statements (addr filled by caller)."""
    m, ops, sz = ins.mnem, ins.ops, ins.opsize
    rsp = Reg("rsp")
    out: List[LInsn] = []

    def add(*s): out.extend(s)

    if m in ("nop",):
        return [LNop()]
    if m == "mov":
        return _write(ops[0], _read(ops[1], sz), sz)
    if m in ("movzx", "movsx", "movsxd"):
        src = _read(ops[1])
        val = Ext("sx", src, ops[1].size) if m != "movzx" else src
        return _write(ops[0], val, sz)
    if m == "lea":
        return _write(ops[0], _addr_expr(ops[1]), sz)
    if m == "push":
        v = _read(ops[0], 8)
        return [LSetReg(0, "rsp", Bin("+", rsp, Const(-8))), LStore(0, rsp, v, 8)]
    if m == "pop":
        return _write(ops[0], Load(rsp, 8), 8) + [LSetReg(0, "rsp", Bin("+", rsp, Const(8)))]
    if m == "leave":
        return [LSetReg(0, "rsp", Reg("rbp")), LSetReg(0, "rbp", Load(rsp, 8)),
                LSetReg(0, "rsp", Bin("+", rsp, Const(8)))]
    if m in ("add", "sub", "cmp", "and", "or", "xor", "test"):
        a, b = _read(ops[0], sz), _read(ops[1], sz)
        b = Const(b.value) if isinstance(b, Const) else b
        if m == "add":
            r = Bin("+", a, b)
            add(LSetFlag(0, "cf", Bin("<u", _zx(r, sz), a)),
                LSetFlag(0, "of", Bin(f"add_of{sz}", a, b)))
            add(*_write(ops[0], r, sz)); add(*_zsp(_read(ops[0], sz), sz))
        elif m in ("sub", "cmp"):
            r = Bin("-", a, b)
            add(LSetFlag(0, "cf", Bin("<u", a, b)), LSetFlag(0, "of", Bin(f"sub_of{sz}", a, b)))
            if m == "sub": add(*_write(ops[0], r, sz))
            add(*_zsp(_read(ops[0], sz) if m == "sub" else r, sz))
        else:
            sym = {"and": "&", "or": "|", "xor": "^", "test": "&"}[m]
            r = Bin(sym, a, b)
            if m != "test": add(*_write(ops[0], r, sz))
            add(*_flags_logic(_read(ops[0], sz) if m != "test" else r, sz))
        return out
    if m in ("inc", "dec"):
        a = _read(ops[0], sz)
        d = Const(1 if m == "inc" else -1)
        r = Bin("+", a, d)
        add(LSetFlag(0, "of", Bin(f"{'add' if m == 'inc' else 'sub'}_of{sz}", a, Const(1))))
        add(*_write(ops[0], r, sz)); add(*_zsp(_read(ops[0], sz), sz))
        return out
    if m == "neg":
        a = _read(ops[0], sz); r = Un("neg", a)
        add(LSetFlag(0, "cf", Bin("!=", a, Const(0))), LSetFlag(0, "of", Bin(f"sub_of{sz}", Const(0), a)))
        add(*_write(ops[0], r, sz)); add(*_zsp(_read(ops[0], sz), sz))
        return out
    if m == "not":
        return _write(ops[0], Un("not", _read(ops[0], sz)), sz)
    if m == "imul":
        if len(ops) == 3: a, b = _read(ops[1], sz), _read(ops[2], sz)
        else: a, b = _read(ops[0], sz), _read(ops[1], sz)
        r = Bin("*", a, b)
        add(*_write(ops[0], r, sz))
        add(LSetFlag(0, "cf", Undef()), LSetFlag(0, "of", Undef()))
        return out
    if m in ("shl", "shr", "sar"):
        a = _read(ops[0], sz); c = _read(ops[1], 1)
        c = fold(Bin("&", c, Const(0x3F if sz == 8 else 0x1F)))
        if m == "shl": r = Bin("<<", a, c)
        elif m == "shr": r = Bin(">>", a, c)
        else: r = Bin("s>>", _sx(a, sz), c)
        add(LSetFlag(0, "cf", Undef()), LSetFlag(0, "of", Undef()))
        add(*_write(ops[0], r, sz)); add(*_zsp(_read(ops[0], sz), sz))
        return out
    if m in ("div", "idiv"):
        if sz not in (4, 8): return [LUnimpl(0, f"{m}{sz * 8}")]
        s = _read(ops[0], sz); a = _read(Opnd("reg", sz, "rax"), sz)
        sgn = m == "idiv"
        if sgn: a, s = _sx(a, sz), _sx(s, sz)
        q = Bin("/s" if sgn else "/u", a, s); rm = Bin("%s" if sgn else "%u", a, s)
        add(*_write(Opnd("reg", sz, "rdx"), rm, sz), *_write(Opnd("reg", sz, "rax"), q, sz))
        return out
    if m in ("cdqe", "cwde"):
        return _write(Opnd("reg", sz, "rax"), Ext("sx", Reg("rax"), 4 if m == "cdqe" else 2), sz)
    if m in ("cdq", "cqo"):
        a = _sx(_read(Opnd("reg", sz, "rax"), sz), sz)
        return _write(Opnd("reg", sz, "rdx"), Bin("s>>", a, Const(sz * 8 - 1)), sz)
    if m == "setcc":
        return _write(ops[0], Tern(Cond(ins.cc), Const(1), Const(0)), 1)
    if m == "cmovcc":
        return _write(ops[0], Tern(Cond(ins.cc), _read(ops[1], sz), _read(ops[0], sz)), sz)
    if m == "jmp":
        if ins.target is not None: return [LJump(0, Const(ins.target))]
        return [LJump(0, _read(ops[0], 8))]
    if m == "jcc":
        return [LIf(0, Cond(ins.cc), ins.target, ins.next_addr)]
    if m == "call":
        tgt = Const(ins.target) if ins.target is not None else _read(ops[0], 8)
        return [LSetReg(0, "rsp", Bin("+", rsp, Const(-8))),
                LStore(0, rsp, Const(ins.next_addr), 8, retaddr=True),
                LCall(0, tgt),
                LSetReg(0, "rsp", Bin("+", rsp, Const(8)))]  # callee's `ret` pops it
    if m == "ret":
        return [LSetReg(0, "rsp", Bin("+", rsp, Const(8))), LRet(0, Load(Bin("+", rsp, Const(-8)), 8))]
    if m in ("int3", "ud2", "hlt"):
        return [LTrap(0, m)]
    return [LUnimpl(0, m)]


# ---------------------------------------------------------------------------
# Binary containers
# ---------------------------------------------------------------------------

class UnsupportedBinary(Exception):
    pass


@dataclass
class Segment:
    addr: int
    data: bytes
    executable: bool
    name: str = ""


class BinaryView:
    """A loaded image: memory segments + symbol/import tables + entry point."""

    def __init__(self, fmt: str, entry: int, segments: List[Segment],
                 symbols: Dict[int, str], imports: Dict[int, str]):
        self.fmt, self.entry = fmt, entry
        self.segments = segments
        self.symbols = symbols      # function addr -> name
        self.imports = imports      # PLT stub / IAT slot addr -> name

    def read(self, addr: int, n: int) -> Optional[bytes]:
        for s in self.segments:
            if s.addr <= addr and addr + n <= s.addr + len(s.data):
                return s.data[addr - s.addr: addr - s.addr + n]
        return None

    def segment_at(self, addr: int) -> Optional[Segment]:
        for s in self.segments:
            if s.addr <= addr < s.addr + len(s.data):
                return s
        return None

    def is_executable(self, addr: int) -> bool:
        s = self.segment_at(addr)
        return bool(s and s.executable)

    # -- loaders -----------------------------------------------------------
    @classmethod
    def from_bytes(cls, data: bytes, fmt: Optional[str] = None, base: int = 0x1000,
                   entry: Optional[int] = None) -> "BinaryView":
        if fmt is None:
            fmt = "elf" if data[:4] == b"\x7fELF" else "pe" if data[:2] == b"MZ" else "raw"
        if fmt == "elf": return cls._load_elf(data)
        if fmt == "pe": return cls._load_pe(data)
        if fmt == "raw":
            e = base if entry is None else entry
            return cls("raw", e, [Segment(base, bytes(data), True, ".raw")], {e: "entry"}, {})
        raise UnsupportedBinary(f"unknown format {fmt!r}")

    @classmethod
    def open(cls, path: str, **kw) -> "BinaryView":
        with open(path, "rb") as f:
            return cls.from_bytes(f.read(), **kw)

    @classmethod
    def _load_elf(cls, d: bytes) -> "BinaryView":
        if len(d) < 64 or d[:4] != b"\x7fELF": raise UnsupportedBinary("not ELF")
        if d[4] != 2 or d[5] != 1: raise UnsupportedBinary("only little-endian ELF64 is supported")
        machine, = struct.unpack_from("<H", d, 0x12)
        if machine != 62: raise UnsupportedBinary(f"unsupported ELF machine {machine} (x86-64 only)")
        entry, phoff, shoff = struct.unpack_from("<QQQ", d, 0x18)
        phentsize, phnum, shentsize, shnum, shstrndx = struct.unpack_from("<HHHHH", d, 0x36)
        segs: List[Segment] = []
        for k in range(phnum):
            p_type, p_flags, p_off, p_va, _pa, p_fsz, p_msz, _al = struct.unpack_from(
                "<IIQQQQQQ", d, phoff + k * phentsize)
            if p_type != 1: continue
            body = d[p_off:p_off + p_fsz] + b"\0" * max(0, p_msz - p_fsz)
            segs.append(Segment(p_va, body, bool(p_flags & 1)))
        secs = []
        for k in range(shnum):
            secs.append(struct.unpack_from("<IIQQQQIIQQ", d, shoff + k * shentsize))
        def cstr(off):
            e = d.index(b"\0", off); return d[off:e].decode("latin1")
        names = {}
        if shnum and shstrndx < shnum:
            stroff = secs[shstrndx][4]
            for k, s in enumerate(secs): names[k] = cstr(stroff + s[0])
        symbols: Dict[int, str] = {}
        dynsyms: List[str] = []
        for k, s in enumerate(secs):
            sh_type, _fl, _addr, off, size, link = s[1], s[2], s[3], s[4], s[5], s[6]
            if sh_type not in (2, 11): continue          # SYMTAB / DYNSYM
            stroff = secs[link][4]
            for q in range(size // 24):
                st_name, st_info, _o, st_shndx, st_val, _sz = struct.unpack_from("<IBBHQQ", d, off + q * 24)
                nm = cstr(stroff + st_name) if st_name else ""
                if sh_type == 11: dynsyms.append(nm)
                if st_info & 0xF == 2 and st_shndx != 0 and st_val and nm:
                    symbols.setdefault(st_val, nm)
        imports: Dict[int, str] = {}
        by_name = {v: k for k, v in names.items()}
        relplt = by_name.get(".rela.plt")
        if relplt is not None and dynsyms:
            off, size = secs[relplt][4], secs[relplt][5]
            plt_names: List[str] = []
            for q in range(size // 24):
                _roff, info, _add = struct.unpack_from("<QQq", d, off + q * 24)
                si = info >> 32
                plt_names.append(dynsyms[si] if si < len(dynsyms) else f"plt_{q}")
            if ".plt.sec" in by_name:
                base, step0 = secs[by_name[".plt.sec"]][3], 0
            elif ".plt" in by_name:
                base, step0 = secs[by_name[".plt"]][3], 1
            else:
                base, step0 = None, 0
            if base is not None:
                for q, nm in enumerate(plt_names):
                    imports[base + 16 * (q + step0)] = nm
        return cls("elf", entry, segs, symbols, imports)

    @classmethod
    def _load_pe(cls, d: bytes) -> "BinaryView":
        if d[:2] != b"MZ": raise UnsupportedBinary("not PE")
        pe, = struct.unpack_from("<I", d, 0x3C)
        if d[pe:pe + 4] != b"PE\0\0": raise UnsupportedBinary("bad PE signature")
        machine, nsec, _ts, _sp, _ns, optsz, _ch = struct.unpack_from("<HHIIIHH", d, pe + 4)
        if machine != 0x8664: raise UnsupportedBinary(f"unsupported PE machine {machine:#x} (x86-64 only)")
        opt = pe + 24
        if struct.unpack_from("<H", d, opt)[0] != 0x20B: raise UnsupportedBinary("only PE32+ is supported")
        entry_rva, = struct.unpack_from("<I", d, opt + 16)
        image_base, = struct.unpack_from("<Q", d, opt + 24)
        dd = opt + 112
        exp_rva, _ = struct.unpack_from("<II", d, dd)
        imp_rva, _ = struct.unpack_from("<II", d, dd + 8)
        secs = []
        sh = opt + optsz
        for k in range(nsec):
            nm = d[sh + k * 40: sh + k * 40 + 8].rstrip(b"\0").decode("latin1")
            vsz, va, rsz, roff = struct.unpack_from("<IIII", d, sh + k * 40 + 8)
            chars, = struct.unpack_from("<I", d, sh + k * 40 + 36)
            secs.append((nm, va, vsz, roff, rsz, chars))
        segs = []
        for nm, va, vsz, roff, rsz, ch in secs:
            raw = d[roff:roff + min(rsz, vsz or rsz)]
            raw += b"\0" * max(0, (vsz or rsz) - len(raw))
            segs.append(Segment(image_base + va, raw, bool(ch & 0x20000000), nm))
        def rva_off(rva):
            for _nm, va, vsz, roff, rsz, _c in secs:
                if va <= rva < va + max(vsz, rsz): return roff + rva - va
            return None
        def cstr(off):
            e = d.index(b"\0", off); return d[off:e].decode("latin1")
        symbols: Dict[int, str] = {image_base + entry_rva: "entry"}
        if exp_rva and (eo := rva_off(exp_rva)) is not None:
            nfun, nnames, afun, anam, aord = struct.unpack_from("<IIIII", d, eo + 20)
            fo, no, oo = rva_off(afun), rva_off(anam), rva_off(aord)
            for q in range(nnames):
                nrva, = struct.unpack_from("<I", d, no + 4 * q)
                ordn, = struct.unpack_from("<H", d, oo + 2 * q)
                frva, = struct.unpack_from("<I", d, fo + 4 * ordn)
                symbols[image_base + frva] = cstr(rva_off(nrva))
        imports: Dict[int, str] = {}
        if imp_rva and (io := rva_off(imp_rva)) is not None:
            while True:
                oft, _t, _f, name_rva, ft = struct.unpack_from("<IIIII", d, io)
                if not name_rva: break
                dll = cstr(rva_off(name_rva))
                src = rva_off(oft or ft); q = 0
                while src is not None:
                    th, = struct.unpack_from("<Q", d, src + 8 * q)
                    if not th: break
                    if not th >> 63:
                        imports[image_base + ft + 8 * q] = f"{dll}!{cstr(rva_off(th & 0x7FFFFFFF) + 2)}"
                    q += 1
                io += 20
        return cls("pe", image_base + entry_rva, segs, symbols, imports)


# ---------------------------------------------------------------------------
# MLIL
# ---------------------------------------------------------------------------

def _collect_vars(e: Expr, acc: List[Var]) -> None:
    if isinstance(e, Var): acc.append(e)
    elif isinstance(e, AddrOf): return
    else:
        for c in e.children(): _collect_vars(c, acc)


def subst_vars(e: Expr, fn: Callable[[Var], Expr]) -> Expr:
    if isinstance(e, Var): return fn(e)
    if isinstance(e, AddrOf): return e
    return e.map(lambda c: subst_vars(c, fn))


def expr_has_load(e: Expr) -> bool:
    return isinstance(e, Load) or any(expr_has_load(c) for c in e.children())


class MInsn:
    """Base MLIL instruction.  ``vars_read`` / ``vars_written`` are cached; in SSA
    form the Var objects carry their version."""
    block: int = -1
    addr: int = 0

    def exprs(self) -> Tuple[Expr, ...]:
        return ()

    def written(self) -> Tuple[Var, ...]:
        return ()

    @cached_property
    def vars_read(self) -> Tuple[Var, ...]:
        acc: List[Var] = []
        for e in self.exprs(): _collect_vars(e, acc)
        return tuple(acc)

    @cached_property
    def vars_written(self) -> Tuple[Var, ...]:
        return tuple(self.written())

    is_terminator = False
    has_side_effects = False


class MSet(MInsn):
    def __init__(self, dest: Var, src: Expr, addr: int = 0):
        self.dest, self.src, self.addr = dest, src, addr
    def exprs(self): return (self.src,)
    def written(self): return (self.dest,)
    def __str__(self): return f"{self.dest} = {self.src}"


class MStore(MInsn):
    has_side_effects = True
    def __init__(self, mem_addr: Expr, src: Expr, size: int, addr: int = 0):
        self.mem_addr, self.src, self.size, self.addr = mem_addr, src, size, addr
    def exprs(self): return (self.mem_addr, self.src)
    def __str__(self): return f"*({_TY.get(self.size, 'u?')}*){fmt_expr(self.mem_addr, 9)} = {self.src}"


class MCall(MInsn):
    has_side_effects = True
    def __init__(self, target: Expr, args: List[Expr], outs: List[Var], addr: int = 0):
        self.target, self.args, self.outs, self.addr = target, list(args), list(outs), addr
    def exprs(self): return (self.target, *self.args)
    def written(self): return tuple(self.outs)
    def __str__(self):
        a = ", ".join(map(str, self.args))
        o = ", ".join(map(str, self.outs))
        return f"{o} = call {self.target}({a})" if o else f"call {self.target}({a})"


class MRet(MInsn):
    is_terminator = True
    def __init__(self, value: Expr, addr: int = 0):
        self.value, self.addr = value, addr
    def exprs(self): return (self.value,)
    def __str__(self): return f"return {self.value}"


class MIf(MInsn):
    is_terminator = True
    def __init__(self, cond: Expr, t: int, f: int, addr: int = 0):
        self.cond, self.t, self.f, self.addr = cond, t, f, addr
    def exprs(self): return (self.cond,)
    def __str__(self): return f"if ({self.cond}) goto B{self.t} else B{self.f}"


class MGoto(MInsn):
    is_terminator = True
    def __init__(self, target: int, addr: int = 0):
        self.target, self.addr = target, addr
    def __str__(self): return f"goto B{self.target}"


class MTrap(MInsn):
    is_terminator = True
    has_side_effects = True
    def __init__(self, kind: str, args: Tuple[Expr, ...] = (), addr: int = 0):
        self.kind, self.args, self.addr = kind, tuple(args), addr
    def exprs(self): return self.args
    def __str__(self):
        return f"{self.kind}({', '.join(map(str, self.args))})" if self.args else self.kind


class MUnimpl(MInsn):
    has_side_effects = True
    def __init__(self, text: str, addr: int = 0):
        self.text, self.addr = text, addr
    def __str__(self): return f"/* unimplemented: {self.text} */"


class MPhi(MInsn):
    def __init__(self, dest: Var, srcs: Dict[int, Var], addr: int = 0):
        self.dest, self.srcs, self.addr = dest, dict(srcs), addr  # pred block -> Var
    def exprs(self): return tuple(self.srcs.values())
    def written(self): return (self.dest,)
    def __str__(self):
        return f"{self.dest} = phi({', '.join(f'B{b}: {v}' for b, v in sorted(self.srcs.items()))})"


def terminator_succs(t: MInsn) -> List[int]:
    if isinstance(t, MIf): return [t.t] if t.t == t.f else [t.t, t.f]
    if isinstance(t, MGoto): return [t.target]
    return []


@dataclass
class MLILBlock:
    index: int
    start: int
    insns: List[MInsn] = field(default_factory=list)
    preds: List[int] = field(default_factory=list)

    @property
    def succs(self) -> List[int]:
        return terminator_succs(self.insns[-1]) if self.insns else []


@dataclass
class MLILFunction:
    name: str
    blocks: List[MLILBlock]
    escaped: frozenset = frozenset()         # variables whose address is taken
    params: Tuple[str, ...] = ()
    widths: Dict[str, int] = field(default_factory=dict)   # frame-slot name -> byte width

    def recompute_preds(self) -> None:
        for b in self.blocks: b.preds = []
        for b in self.blocks:
            for s in b.succs:
                if b.index not in self.blocks[s].preds: self.blocks[s].preds.append(b.index)

    def __str__(self):
        out = [f"{self.name}:"]
        for b in self.blocks:
            out.append(f"  B{b.index} @ {b.start:#x}  preds={b.preds}")
            out += [f"    {i}" for i in b.insns]
        return "\n".join(out)


def slot_name(delta: int) -> str:
    if delta < 0: return f"var_{-delta:x}"
    if delta == 0: return "retaddr"
    return f"arg_{delta - 8:x}"


class MLILBuilder:
    """LLIL -> MLIL: variables, frame slots, flag-folded conditions."""

    def __init__(self, llil: LLILFunction, name: str, binary: Optional[BinaryView] = None):
        self.llil, self.name, self.binary = llil, name, binary
        self.escaped: set = set()
        self.slot_size: Dict[int, int] = {}

    # --- frame arithmetic ---------------------------------------------------
    @staticmethod
    def frame_eval(e: Expr, sp: Optional[int], bp: Optional[int]) -> Optional[int]:
        if isinstance(e, Reg):
            return sp if e.name == "rsp" else bp if e.name == "rbp" else None
        if isinstance(e, Bin) and e.op in ("+", "-"):
            if isinstance(e.r, Const):
                b = MLILBuilder.frame_eval(e.l, sp, bp)
                if b is not None:
                    return (b + (e.r.value if e.op == "+" else -e.r.value) + (1 << 63)) % (1 << 64) - (1 << 63)
            if e.op == "+" and isinstance(e.l, Const):
                b = MLILBuilder.frame_eval(e.r, sp, bp)
                if b is not None:
                    return (b + e.l.value + (1 << 63)) % (1 << 64) - (1 << 63)
        return None

    @staticmethod
    def _mentions_frame(e: Expr) -> bool:
        if isinstance(e, Load):      # [rbp-4] reads a slot; it does not take an address
            return False
        return (isinstance(e, Reg) and e.name in ("rsp", "rbp")) or \
            any(MLILBuilder._mentions_frame(c) for c in e.children())

    # --- condition lowering ---------------------------------------------------
    @staticmethod
    def _cmp_pairs(F: Dict[str, Expr]) -> List[Tuple[Expr, Expr, int]]:
        """Candidate (a, b, width) operand pairs of a subtraction behind the flags."""
        c: List[Tuple[Expr, Expr, int]] = []
        of, cf = F.get("of"), F.get("cf")
        if isinstance(of, Bin) and of.op.startswith("sub_of"):
            c.append((of.l, of.r, int(of.op[6:])))
        if isinstance(cf, Bin) and cf.op == "<u":
            c += [(cf.l, cf.r, n) for n in (8, 4, 2, 1)]
        return c

    def build_cond(self, cc: str, F: Dict[str, Expr]) -> Expr:
        g = lambda n: F.get(n, Var(n))
        zf, sf, cf, of, pf = g("zf"), g("sf"), g("cf"), g("of"), g("pf")
        eq: Expr = zf
        pair = None          # (a, b) when zf/sf/of/cf all describe a - b
        scmp = None
        for a, b, n in self._cmp_pairs(F):
            r = fold(Bin("-", a, b))
            if fold(Bin("==", _zx(r, n), Const(0))) == zf:
                eq, pair = Bin("==", a, b), (a, b)
                if isinstance(of, Bin) and of.op.startswith("sub_of") and (of.l, of.r) == (a, b) \
                        and fold(Bin("<s", _sx(r, n), Const(0))) == sf:
                    scmp = (_sx(a, n), _sx(b, n))
                break
        ne = Un("!", eq)
        ucmp = isinstance(cf, Bin) and cf.op == "<u"
        if cc == "e": r = eq
        elif cc == "ne": r = ne
        elif cc in ("l", "ge", "le", "g"):
            if scmp:
                r = Bin({"l": "<s", "ge": ">=s", "le": "<=s", "g": ">s"}[cc], *scmp)
            elif isinstance(of, Const) and of.value == 0:
                r = {"l": sf, "ge": Un("!", sf), "le": Bin("|", eq, sf),
                     "g": Bin("&", Un("!", eq), Un("!", sf))}[cc]
            else:
                ltx = Bin("!=", sf, of)
                r = {"l": ltx, "ge": Un("!", ltx), "le": Bin("|", eq, ltx),
                     "g": Bin("&", Un("!", eq), Un("!", ltx))}[cc]
        elif cc in ("b", "ae", "be", "a"):
            if ucmp and (cc in ("b", "ae") or pair == (cf.l, cf.r)):
                r = Bin({"b": "<u", "ae": ">=u", "be": "<=u", "a": ">u"}[cc], cf.l, cf.r)
            else:
                r = {"b": cf, "ae": Un("!", cf), "be": Bin("|", cf, eq),
                     "a": Bin("&", Un("!", cf), Un("!", eq))}[cc]
        elif cc == "s": r = sf
        elif cc == "ns": r = Un("!", sf)
        elif cc == "o": r = of
        elif cc == "no": r = Un("!", of)
        elif cc == "p": r = pf
        else: r = Un("!", pf)
        return fold(r)

    # --- main ---------------------------------------------------------------
    def build(self) -> MLILFunction:
        llil = self.llil
        order: List[int] = []            # LLIL block indices in BFS order
        idx_of = {b.start: k for k, b in enumerate(llil.blocks)}
        lpreds: Dict[int, List[int]] = {k: [] for k in range(len(llil.blocks))}
        for k, b in enumerate(llil.blocks):
            for s in b.succs:
                if s in idx_of: lpreds[idx_of[s]].append(k)
        seen, q = {0}, [0]
        while q:
            k = q.pop(0); order.append(k)
            for s in llil.blocks[k].succs:
                j = idx_of.get(s)
                if j is not None and j not in seen: seen.add(j); q.append(j)
        mindex = {k: n for n, k in enumerate(order)}   # LLIL idx -> MLIL idx

        out_state: Dict[int, dict] = {}
        mblocks: List[MLILBlock] = []
        imports = self.binary.imports if self.binary else {}

        for k in order:
            lb = llil.blocks[k]
            mb = MLILBlock(mindex[k], lb.start)
            ps = [p for p in lpreds[k] if p in out_state]
            if k == 0 or not ps:
                st = dict(sp=0, bp=None, flags={}, argdefs=set())
            else:
                first = out_state[ps[0]]
                st = dict(sp=first["sp"], bp=first["bp"],
                          flags=dict(first["flags"]) if len(lpreds[k]) == 1 else {},
                          argdefs=set().union(*[out_state[p]["argdefs"] for p in ps]))
            ins_out = mb.insns
            flags: Dict[str, Expr] = st["flags"]

            def invalidate(written: Iterable[str], mem: bool = False, _flags=flags, _out=ins_out, _addr=lb.start):
                w = set(written)
                for f, e in list(_flags.items()):
                    rd: List[Var] = []; _collect_vars(e, rd)
                    if (any(v.name in w for v in rd) or (mem and expr_has_load(e))) and not isinstance(e, (Var, Undef)):
                        _out.append(MSet(Var(f), e, _addr))
                        _flags[f] = Var(f)

            def conv(e: Expr, sp, bp) -> Expr:
                if isinstance(e, (Reg, Bin)):
                    d = self.frame_eval(e, sp, bp)
                    if d is not None:
                        v = Var(slot_name(d)); self.escaped.add(v.name); return AddrOf(v)
                if isinstance(e, Const): return e
                if isinstance(e, Reg): return Var(e.name)
                if isinstance(e, Flag): return flags.get(e.name, Var(e.name))
                if isinstance(e, Cond): return self.build_cond(e.cc, flags)
                if isinstance(e, Load):
                    d = self.frame_eval(e.addr, sp, bp)
                    if d is not None:
                        v = Var(slot_name(d))
                        w = self.slot_size.setdefault(d, e.size)
                        return v if e.size >= w else Ext("zx", v, e.size)
                    return Load(conv(e.addr, sp, bp), e.size)
                if isinstance(e, Undef): return e
                return fold(e.map(lambda c: conv(c, sp, bp)))

            sp, bp = st["sp"], st["bp"]
            argdefs: set = st["argdefs"]
            terminated = False
            for li in lb.insns:
                a = li.addr
                if isinstance(li, LSetReg):
                    if li.reg in ("rsp", "rbp"):
                        d = self.frame_eval(li.src, sp, bp)
                        if li.reg == "rsp": sp = d
                        else: bp = d
                        if d is not None: continue
                    v = conv(li.src, sp, bp)
                    invalidate([li.reg])
                    ins_out.append(MSet(Var(li.reg), v, a))
                    if li.reg in SYSV_ARGS: argdefs.add(li.reg)
                elif isinstance(li, LSetFlag):
                    # flags of stack-pointer arithmetic are never consumed; keep them out
                    # of the frame model rather than treating them as address-taking
                    if self._mentions_frame(li.src):
                        flags[li.flag] = Undef()
                    else:
                        flags[li.flag] = conv(li.src, sp, bp)
                elif isinstance(li, LStore):
                    if li.retaddr: continue
                    d = self.frame_eval(li.mem_addr, sp, bp)
                    val = conv(li.src, sp, bp)
                    if d is not None:
                        w = self.slot_size.setdefault(d, li.size)
                        name = slot_name(d)
                        invalidate([name])
                        ins_out.append(MSet(Var(name), fold(Ext("zx", val, li.size)) if li.size < 8 else val, a))
                    else:
                        invalidate([], mem=True)
                        ins_out.append(MStore(conv(li.mem_addr, sp, bp), val, li.size, a))
                elif isinstance(li, LCall):
                    tgt = conv(li.target, sp, bp)
                    if isinstance(tgt, Load) and isinstance(tgt.addr, Const) and tgt.addr.value in imports:
                        tgt = Var(imports[tgt.addr.value])      # PE IAT slot
                    args = [Var(r) for r in SYSV_ARGS if r in argdefs]
                    invalidate(CALLER_SAVED, mem=True)
                    ins_out.append(MCall(tgt, args, [Var(r) for r in CALLER_SAVED], a))
                    argdefs.clear()
                    for f in ("cf", "pf", "zf", "sf", "of"): flags[f] = Undef()
                elif isinstance(li, LJump):
                    if isinstance(li.target, Const):
                        j = idx_of.get(li.target.value)
                        if j is not None and j in mindex:
                            self._join_flush(ins_out, flags, lpreds, j, a); ins_out.append(MGoto(mindex[j], a))
                        else:
                            ins_out.append(MTrap("jump_outside_function", (li.target,), a))
                    else:
                        ins_out.append(MTrap("indirect_jump", (conv(li.target, sp, bp),), a))
                    terminated = True
                elif isinstance(li, LIf):
                    cond = self.build_cond(li.cond.cc, flags)
                    tj, fj = idx_of.get(li.true_addr), idx_of.get(li.false_addr)
                    if tj is None or fj is None or tj not in mindex or fj not in mindex:
                        ins_out.append(MTrap("branch_outside_function", (cond,), a))
                    else:
                        self._join_flush(ins_out, flags, lpreds, tj, a)
                        self._join_flush(ins_out, flags, lpreds, fj, a)
                        ins_out.append(MIf(cond, mindex[tj], mindex[fj], a))
                    terminated = True
                elif isinstance(li, LRet):
                    ins_out.append(MRet(Var("rax"), a)); terminated = True
                elif isinstance(li, LTrap):
                    ins_out.append(MTrap(li.reason, (), a)); terminated = True
                elif isinstance(li, LUnimpl):
                    ins_out.append(MUnimpl(li.text, a))
                # LNop: nothing
            if not terminated:
                nxt = lb.succs[0] if lb.succs else None
                j = idx_of.get(nxt) if nxt is not None else None
                if j is not None and j in mindex:
                    self._join_flush(ins_out, flags, lpreds, j, lb.start)
                    ins_out.append(MGoto(mindex[j], lb.start))
                else:
                    ins_out.append(MTrap("fallthrough_out_of_function", (), lb.start))
            out_state[k] = dict(sp=sp, bp=bp, flags=dict(flags), argdefs=argdefs)
            mblocks.append(mb)

        for b in mblocks:
            for ins in b.insns: ins.block = b.index
        f = MLILFunction(self.name, mblocks, frozenset(self.escaped),
                         widths={slot_name(d): w for d, w in self.slot_size.items()})
        f.recompute_preds()
        return f

    @staticmethod
    def _join_flush(out, flags, lpreds, target_idx, addr):
        """Before entering a join block, flags must live in variables."""
        if len(lpreds[target_idx]) > 1:
            for fl, e in list(flags.items()):
                if not isinstance(e, Var):
                    out.append(MSet(Var(fl), e, addr))
                    # keep symbolic value for later in-block uses; the join block reads Var(fl)


# ---------------------------------------------------------------------------
# Function / Lifter
# ---------------------------------------------------------------------------

class Function:
    def __init__(self, lifter: "Lifter", start: int, name: str, llil: LLILFunction):
        self.lifter, self.start, self.name, self.llil = lifter, start, name, llil

    @cached_property
    def mlil(self) -> MLILFunction:
        return MLILBuilder(self.llil, self.name, self.lifter.binary).build()

    @cached_property
    def ssa(self):
        from .ssa_tracer import to_ssa
        return to_ssa(self.mlil)

    @cached_property
    def optimized_ssa(self):
        from .ssa_tracer import optimize
        return optimize(self.ssa)

    @cached_property
    def hlil(self):
        from .decompiler import decompile
        return decompile(self.optimized_ssa.to_mlil(), self.lifter)

    def __repr__(self):
        return f"<Function {self.name} @ {self.start:#x}>"


class Lifter:
    """Headless front-end: open a binary, index functions, expose IR tiers."""

    def __init__(self, binary: BinaryView):
        self.binary = binary
        self.functions: Dict[int, Function] = {}
        self._index()

    @classmethod
    def open(cls, path: str, **kw) -> "Lifter":
        return cls(BinaryView.open(path, **kw))

    @classmethod
    def from_bytes(cls, data: bytes, fmt: Optional[str] = None, base: int = 0x1000, **kw) -> "Lifter":
        return cls(BinaryView.from_bytes(data, fmt=fmt, base=base, **kw))

    def name_of(self, addr: int) -> Optional[str]:
        b = self.binary
        return b.symbols.get(addr) or b.imports.get(addr) or (
            self.functions[addr].name if addr in self.functions else None)

    def get_function(self, key) -> Optional[Function]:
        if isinstance(key, int): return self.functions.get(key)
        for f in self.functions.values():
            if f.name == key: return f
        return None

    # -- discovery ------------------------------------------------------------
    def _disassemble(self, start: int, boundaries: set):
        """Recursive descent from `start`; returns ({addr: Insn}, [call targets])."""
        insns: Dict[int, Insn] = {}
        calls: List[int] = []
        work = [start]
        while work:
            a = work.pop()
            while a not in insns:
                seg = self.binary.segment_at(a)
                if seg is None or not seg.executable: break
                try:
                    ins = decode(seg.data, a - seg.addr, a)
                except DecodeError:
                    insns[a] = Insn(a, 1, "undecodable"); break
                insns[a] = ins
                if ins.mnem == "call" and ins.target is not None: calls.append(ins.target)
                if ins.mnem == "jcc":
                    work.append(ins.target)
                elif ins.mnem == "jmp":
                    if ins.target is not None and ins.target != start and ins.target in boundaries:
                        break                                    # tail call
                    if ins.target is not None: work.append(ins.target)
                    break
                elif ins.mnem in ("ret", "ud2", "hlt", "int3"):
                    break
                a = ins.next_addr
        return insns, calls

    def _lift_function(self, start: int, name: str, boundaries: set) -> Tuple[LLILFunction, List[int]]:
        insns, calls = self._disassemble(start, boundaries)
        stmts: List[LInsn] = []
        by_addr: Dict[int, List[LInsn]] = {}
        for a in sorted(insns):
            ins = insns[a]
            if ins.mnem == "undecodable":
                l = [LTrap(a, "undecodable")]
            elif ins.mnem == "jmp" and ins.target is not None and ins.target != start and ins.target in boundaries:
                l = [LCall(a, Const(ins.target)), LRet(a, Load(Reg("rsp"), 8))]   # tail call
            else:
                l = lift_insn(ins, self.binary)
                for s in l: s.addr = a
            by_addr[a] = l
            stmts += l
        leaders = {start}
        for a, ls in by_addr.items():
            for s in ls:
                if isinstance(s, LJump) and isinstance(s.target, Const): leaders.add(s.target.value)
                if isinstance(s, LIf): leaders |= {s.true_addr, s.false_addr}
            if ls and isinstance(ls[-1], LLIL_TERMINATORS):
                nxt = insns[a].next_addr
                if nxt in insns: leaders.add(nxt)
        leaders = {l for l in leaders if l in insns}
        blocks: List[LLILBlock] = []
        cur: Optional[LLILBlock] = None
        for a in sorted(insns):
            if a in leaders or cur is None:
                cur = LLILBlock(a); blocks.append(cur)
            cur.insns += by_addr[a]
            last = by_addr[a][-1] if by_addr[a] else None
            nxt = insns[a].next_addr
            if last is not None and isinstance(last, LLIL_TERMINATORS):
                if isinstance(last, LJump) and isinstance(last.target, Const): cur.succs = [last.target.value]
                elif isinstance(last, LIf): cur.succs = [last.true_addr, last.false_addr]
                cur = None
            elif nxt in leaders or nxt not in insns:
                cur.succs = [nxt] if nxt in insns else []
                cur = None
        # entry block first
        blocks.sort(key=lambda b: (b.start != start, b.start))
        return LLILFunction(start, stmts, blocks), calls

    def _index(self) -> None:
        b = self.binary
        starts: Dict[int, str] = {}
        for a, n in b.symbols.items():
            if b.is_executable(a): starts[a] = n
        if b.is_executable(b.entry): starts.setdefault(b.entry, b.symbols.get(b.entry, "_start"))
        boundaries = set(starts) | set(b.imports)
        pending = list(starts)
        done: set = set()
        while pending:
            a = pending.pop(0)
            if a in done or a in b.imports or not b.is_executable(a): continue
            done.add(a)
            name = starts.get(a) or f"sub_{a:x}"
            llil, calls = self._lift_function(a, name, boundaries)
            self.functions[a] = Function(self, a, name, llil)
            for c in calls:
                if c not in starts and b.is_executable(c) and c not in b.imports:
                    starts[c] = f"sub_{c:x}"; boundaries.add(c); pending.append(c)
