"""HLIL: structural reconstruction (if/else, while, do-while, for, break/continue)
from an MLIL CFG, plus textual rendering and block tokenisation.

The structurer is dominance-based: loop headers come from natural loops, merge
points from immediate post-dominators.  Anything it cannot express structurally
(irreducible flow, multi-exit loops) is emitted as ``goto`` + label instead of
being mis-structured.
"""
from __future__ import annotations

import re
from dataclasses import dataclass, field
from typing import Dict, List, Optional, Set, Tuple

from .lifter import (
    AddrOf, Bin, Const, Expr, Ext, Load, MCall, MGoto, MIf, MInsn, MLILFunction, MRet,
    MSet, MStore, MTrap, MUnimpl, Un, Var, fmt_expr, fold,
)
from .ssa_tracer import compute_idoms, dominates

# ---------------------------------------------------------------------------
# Pseudo-expressions for readable memory access
# ---------------------------------------------------------------------------


@dataclass(frozen=True)
class HIndex(Expr):
    base: Expr
    idx: Expr
    size: int

    def children(self): return (self.base, self.idx)
    def rebuild(self, k): return HIndex(k[0], k[1], self.size)

    def fmt(self, prec=0):
        b = self.base.var.name if isinstance(self.base, AddrOf) else fmt_expr(self.base, 9)
        return f"{b}[{fmt_expr(self.idx)}]"


@dataclass(frozen=True)
class HMember(Expr):
    base: Expr
    off: int
    size: int

    def children(self): return (self.base,)
    def rebuild(self, k): return HMember(k[0], self.off, self.size)

    def fmt(self, prec=0):
        if isinstance(self.base, AddrOf):
            return f"{self.base.var.name}.field_{self.off:x}"
        return f"{fmt_expr(self.base, 9)}->field_{self.off:x}"


def _as_scaled(e: Expr, size: int) -> Optional[Expr]:
    if isinstance(e, Bin) and e.op == "*" and isinstance(e.r, Const) and e.r.value == size:
        return e.l
    if isinstance(e, Bin) and e.op == "<<" and isinstance(e.r, Const) and (1 << e.r.value) == size:
        return e.l
    return None


def pretty_deref(addr: Expr, size: int) -> Expr:
    """Turn ``*(T*)(base + idx*size)`` into ``base[idx]`` and ``*(T*)(p + k)``
    into ``p->field_k``; anything else stays a plain dereference."""
    if isinstance(addr, AddrOf):
        return addr.var
    if isinstance(addr, Bin) and addr.op == "+":
        for base, off in ((addr.l, addr.r), (addr.r, addr.l)):
            idx = _as_scaled(off, size)
            if idx is not None:
                return HIndex(base, idx, size)
        if isinstance(addr.r, Const) and 0 <= addr.r.value < 0x1000:
            return HMember(addr.l, addr.r.value, size)
    if size == 1 and isinstance(addr, Bin) and addr.op == "+":
        return HIndex(addr.l, addr.r, 1)
    return Load(addr, size)


_WIDTHS: Dict[str, int] = {}


def prettify(e: Expr) -> Expr:
    e = e.map(prettify)
    if isinstance(e, Load):
        return pretty_deref(e.addr, e.size)
    if isinstance(e, Ext) and e.kind == "zx":
        x = e.e
        if isinstance(x, Var) and _WIDTHS.get(x.name) == e.size: return x       # slot already n bytes wide
        if isinstance(x, (HIndex, HMember)) and x.size == e.size: return x       # memory read is n bytes
    return e


def negate(c: Expr) -> Expr:
    return fold(Un("!", c))


# ---------------------------------------------------------------------------
# HLIL statements
# ---------------------------------------------------------------------------

class HStmt:
    pass


@dataclass
class HAssign(HStmt):
    dest: Expr
    src: Expr


@dataclass
class HCall(HStmt):
    target: str
    args: List[Expr]
    outs: List[Var]


@dataclass
class HIf(HStmt):
    cond: Expr
    then: List[HStmt]
    els: List[HStmt] = field(default_factory=list)


@dataclass
class HWhile(HStmt):
    cond: Optional[Expr]          # None = while (true)
    body: List[HStmt]


@dataclass
class HDoWhile(HStmt):
    body: List[HStmt]
    cond: Expr


@dataclass
class HFor(HStmt):
    init: HAssign
    cond: Expr
    step: HAssign
    body: List[HStmt]


@dataclass
class HReturn(HStmt):
    value: Optional[Expr]


@dataclass
class HBreak(HStmt):
    pass


@dataclass
class HContinue(HStmt):
    pass


@dataclass
class HGoto(HStmt):
    block: int


@dataclass
class HLabel(HStmt):
    block: int


@dataclass
class HRaw(HStmt):
    text: str


_TERMINAL = (HReturn, HBreak, HContinue, HGoto)


@dataclass
class Line:
    depth: int
    text: str
    kind: str  # stmt | if | else | while | do | for | close | label | header


@dataclass
class HLILFunction:
    name: str
    params: Tuple[str, ...]
    body: List[HStmt]

    def lines(self) -> List[Line]:
        out: List[Line] = []
        sig = ", ".join(f"int64_t {p}" for p in self.params)
        out.append(Line(0, f"int64_t {self.name}({sig})", "header"))
        out.append(Line(0, "{", "open"))
        _render(self.body, 1, out)
        out.append(Line(0, "}", "close"))
        return out

    def __str__(self) -> str:
        return "\n".join("    " * l.depth + l.text for l in self.lines())


def _render(stmts: List[HStmt], d: int, out: List[Line]) -> None:
    for s in stmts:
        if isinstance(s, HAssign):
            out.append(Line(d, f"{fmt_expr(s.dest)} = {fmt_expr(s.src)};", "stmt"))
        elif isinstance(s, HCall):
            call = f"{s.target}({', '.join(fmt_expr(a) for a in s.args)})"
            if s.outs:
                lhs = s.outs[0].name if len(s.outs) == 1 else "(" + ", ".join(o.name for o in s.outs) + ")"
                call = f"{lhs} = {call}"
            out.append(Line(d, call + ";", "stmt"))
        elif isinstance(s, HIf):
            out.append(Line(d, f"if ({fmt_expr(s.cond)})", "if"))
            out.append(Line(d, "{", "open")); _render(s.then, d + 1, out); out.append(Line(d, "}", "close"))
            if s.els:
                out.append(Line(d, "else", "else"))
                out.append(Line(d, "{", "open")); _render(s.els, d + 1, out); out.append(Line(d, "}", "close"))
        elif isinstance(s, HWhile):
            c = "true" if s.cond is None else fmt_expr(s.cond)
            out.append(Line(d, f"while ({c})", "while"))
            out.append(Line(d, "{", "open")); _render(s.body, d + 1, out); out.append(Line(d, "}", "close"))
        elif isinstance(s, HDoWhile):
            out.append(Line(d, "do", "do"))
            out.append(Line(d, "{", "open")); _render(s.body, d + 1, out)
            out.append(Line(d, f"}} while ({fmt_expr(s.cond)});", "close"))
        elif isinstance(s, HFor):
            hdr = (f"for ({fmt_expr(s.init.dest)} = {fmt_expr(s.init.src)}; {fmt_expr(s.cond)}; "
                   f"{fmt_expr(s.step.dest)} = {fmt_expr(s.step.src)})")
            out.append(Line(d, hdr, "for"))
            out.append(Line(d, "{", "open")); _render(s.body, d + 1, out); out.append(Line(d, "}", "close"))
        elif isinstance(s, HReturn):
            out.append(Line(d, "return;" if s.value is None else f"return {fmt_expr(s.value)};", "stmt"))
        elif isinstance(s, HBreak):
            out.append(Line(d, "break;", "stmt"))
        elif isinstance(s, HContinue):
            out.append(Line(d, "continue;", "stmt"))
        elif isinstance(s, HGoto):
            out.append(Line(d, f"goto label_{s.block};", "stmt"))
        elif isinstance(s, HLabel):
            out.append(Line(max(d - 1, 0), f"label_{s.block}:", "label"))
        elif isinstance(s, HRaw):
            out.append(Line(d, s.text, "stmt"))


# ---------------------------------------------------------------------------
# Structurer
# ---------------------------------------------------------------------------

@dataclass
class _Loop:
    header: int
    exit: Optional[int]
    body: Set[int]


class Structurer:
    def __init__(self, f: MLILFunction, lifter=None):
        self.f, self.lifter = f, lifter
        self.blocks = f.blocks
        # thread through blocks that only contain a goto, so a `break`/`continue`
        # that lands on a trampoline is seen as targeting the real destination
        fwd = {b.index: b.insns[0].target for b in f.blocks
               if len(b.insns) == 1 and isinstance(b.insns[0], MGoto)}
        for b in list(fwd):                      # drop pure goto cycles (empty infinite loops)
            x, seen = b, set()
            while x in fwd and x not in seen: seen.add(x); x = fwd[x]
            if x in seen: del fwd[b]
        self.fwd = fwd
        self.start = self.T(0)
        self.succs: Dict[int, List[int]] = {}
        for b in f.blocks:
            if b.index in fwd: continue
            ss: List[int] = []
            for s in b.succs:
                s = self.T(s)
                if s not in ss: ss.append(s)
            self.succs[b.index] = ss
        self.idom = compute_idoms(self.succs, self.start)
        self.ipdom = self._postdominators()
        self.loops = self._natural_loops()
        self.emitted: Set[int] = set()
        self.labels: Set[int] = set()

    def T(self, b: int) -> int:
        seen = set()
        while b in self.fwd and b not in seen:
            seen.add(b); b = self.fwd[b]
        return b

    # -- analysis -------------------------------------------------------------
    def _postdominators(self) -> Dict[int, Optional[int]]:
        EXIT = -1
        rev: Dict[int, List[int]] = {EXIT: []}
        for n, ss in self.succs.items():
            rev.setdefault(n, [])
            if not ss: rev[EXIT].append(n)
            for s in ss: rev.setdefault(s, []).append(n)
        pd = compute_idoms(rev, EXIT)
        return {n: (None if d == EXIT else d) for n, d in pd.items() if n != EXIT}

    def _natural_loops(self) -> Dict[int, Set[int]]:
        preds: Dict[int, List[int]] = {n: [] for n in self.succs}
        for n, ss in self.succs.items():
            for s in ss: preds[s].append(n)
        loops: Dict[int, Set[int]] = {}
        for n, ss in self.succs.items():
            for s in ss:
                if n in self.idom and dominates(self.idom, s, n):   # back edge n -> s
                    body = loops.setdefault(s, {s})
                    stack = [n]
                    while stack:
                        x = stack.pop()
                        if x in body: continue
                        body.add(x); stack.extend(preds[x])
        return loops

    # -- instruction conversion -------------------------------------------------
    def _target_name(self, t: Expr) -> str:
        if isinstance(t, Const):
            n = self.lifter.name_of(t.value) if self.lifter else None
            return n or f"sub_{t.value:x}"
        if isinstance(t, Var) and t.version is None and "!" in t.name or (isinstance(t, Var) and "." in t.name):
            return t.name
        if isinstance(t, Var):
            return t.name
        return f"(*{fmt_expr(prettify(t), 9)})"

    def _conv(self, ins: MInsn) -> List[HStmt]:
        if isinstance(ins, MSet):
            if ins.dest == ins.src: return []
            return [HAssign(ins.dest, prettify(ins.src))]
        if isinstance(ins, MStore):
            return [HAssign(pretty_deref(prettify(ins.mem_addr), ins.size), prettify(ins.src))]
        if isinstance(ins, MCall):
            return [HCall(self._target_name(ins.target), [prettify(a) for a in ins.args], list(ins.outs))]
        if isinstance(ins, MUnimpl):
            return [HRaw(f"/* unimplemented: {ins.text} */")]
        return []

    # -- structuring -----------------------------------------------------------------
    def run(self) -> List[HStmt]:
        body = self.seq(self.start, set(), None)
        # blocks referenced by goto but never emitted in-line
        while True:
            todo = sorted(b for b in self.labels if b not in self.emitted)
            if not todo: break
            body.append(HRaw("/* unstructured */"))
            body += self.seq(todo[0], set(), None)
        _prune_labels(body, self.labels)
        return _simplify(body)

    def seq(self, b: Optional[int], stops: Set[int], loop: Optional[_Loop], entry: bool = False) -> List[HStmt]:
        out: List[HStmt] = []
        while b is not None:
            if loop is not None and not entry:
                if b == loop.header: out.append(HContinue()); break
                if b == loop.exit: out.append(HBreak()); break
                if b not in loop.body:
                    self.labels.add(b); out.append(HGoto(b)); break
            if b in stops: break
            if b in self.emitted:
                self.labels.add(b); out.append(HGoto(b)); break
            if b in self.loops and not (loop is not None and loop.header == b and entry):
                stmts, nxt = self._loop(b, loop)
                out += stmts; b = nxt; entry = False; continue
            entry = False
            self.emitted.add(b)
            blk = self.blocks[b]
            out.append(HLabel(b))
            for ins in blk.insns[:-1]: out += self._conv(ins)
            term = blk.insns[-1]
            if isinstance(term, MGoto):
                b = self.T(term.target)
            elif isinstance(term, MRet):
                out.append(HReturn(prettify(term.value))); break
            elif isinstance(term, MTrap):
                if term.kind == "indirect_jump":
                    out.append(HRaw(f"goto *{fmt_expr(prettify(term.args[0]), 9)};"))
                else:
                    out.append(HRaw(f"__{term.kind}();"))
                break
            elif isinstance(term, MIf):
                tt, ff = self.T(term.t), self.T(term.f)
                if tt == ff:
                    b = tt
                    continue
                m = self.ipdom.get(b)
                if loop is not None and m is not None and m not in loop.body:
                    m = None            # merge point lies past the loop: arms use break/continue
                inner = stops | ({m} if m is not None else set())
                then = self.seq(tt, inner, loop)
                els = self.seq(ff, inner, loop)
                cond = prettify(term.cond)
                if not then and els:
                    cond, then, els = negate(cond), els, []
                out.append(HIf(cond, then, els))
                b = m
            else:
                break
        return out

    def _loop(self, h: int, outer: Optional[_Loop]) -> Tuple[List[HStmt], Optional[int]]:
        body_set = self.loops[h]
        term = self.blocks[h].insns[-1]
        exit_ = None
        header_cond = False
        tt = ff = None
        if isinstance(term, MIf):
            tt, ff = self.T(term.t), self.T(term.f)
            outs = [s for s in (tt, ff) if s not in body_set]
            ins_ = [s for s in (tt, ff) if s in body_set]
            if len(outs) == 1 and len(ins_) == 1:
                exit_, header_cond = outs[0], True
        if exit_ is None:
            cands: List[int] = []
            for n in sorted(body_set):
                for s in self.succs[n]:
                    if s not in body_set and s not in cands: cands.append(s)
            exit_ = cands[0] if cands else None
        ctx = _Loop(h, exit_, body_set)
        if header_cond:
            self.emitted.add(h)
            pre: List[HStmt] = []
            for ins in self.blocks[h].insns[:-1]: pre += self._conv(ins)
            inside = tt if tt in body_set else ff
            cond = prettify(term.cond)
            if inside == ff: cond = negate(cond)
            body = self.seq(inside, set(), ctx)
            if not pre:
                return [HWhile(cond, body)], exit_
            return [HWhile(None, pre + [HIf(negate(cond), [HBreak()], [])] + body)], exit_
        body = self.seq(h, set(), ctx, entry=True)
        return [HWhile(None, body)], exit_


# ---------------------------------------------------------------------------
# Tree clean-ups
# ---------------------------------------------------------------------------

def _prune_labels(stmts: List[HStmt], used: Set[int]) -> None:
    stmts[:] = [s for s in stmts if not (isinstance(s, HLabel) and s.block not in used)]
    for s in stmts:
        for sub in _children(s): _prune_labels(sub, used)


def _children(s: HStmt) -> List[List[HStmt]]:
    if isinstance(s, HIf): return [s.then, s.els]
    if isinstance(s, (HWhile, HDoWhile, HFor)): return [s.body]
    return []


def _has_continue(stmts: List[HStmt]) -> bool:
    for s in stmts:
        if isinstance(s, HContinue): return True
        if isinstance(s, HIf) and (_has_continue(s.then) or _has_continue(s.els)): return True
        # continues inside nested loops belong to those loops
    return False


def _vars_in(e: Expr) -> Set[str]:
    acc: Set[str] = set()
    def go(x):
        if isinstance(x, Var): acc.add(x.name)
        elif isinstance(x, AddrOf): acc.add(x.var.name)
        for c in x.children(): go(c)
    go(e)
    return acc


def _simplify(stmts: List[HStmt]) -> List[HStmt]:
    # recurse first
    for s in stmts:
        if isinstance(s, HIf):
            s.then, s.els = _simplify(s.then), _simplify(s.els)
        elif isinstance(s, (HWhile, HDoWhile, HFor)):
            s.body = _simplify(s.body)
    out: List[HStmt] = []
    for s in stmts:
        # if (c) { ...; <terminal> } else { X }  ==>  if (c) {...} X
        if isinstance(s, HIf) and s.els and s.then and isinstance(s.then[-1], _TERMINAL):
            out.append(HIf(s.cond, s.then, [])); out += s.els
        else:
            out.append(s)
    # loop shaping
    res: List[HStmt] = []
    for s in out:
        if isinstance(s, HWhile):
            body = s.body
            while body and isinstance(body[-1], HContinue): body = body[:-1]
            if s.cond is None:
                lead = next((k for k, x in enumerate(body) if not isinstance(x, HLabel)), None)
                if lead is not None and isinstance(body[lead], HIf) and body[lead].then == [HBreak()] \
                        and not body[lead].els:
                    s = HWhile(negate(body[lead].cond), body[:lead] + body[lead + 1:])
                elif body and isinstance(body[-1], HIf) and body[-1].then == [HBreak()] and not body[-1].els \
                        and not _has_continue(body[:-1]):
                    s = HDoWhile(body[:-1], negate(body[-1].cond))
                elif len(body) >= 2 and isinstance(body[-2], HIf) and body[-2].then == [HContinue()] \
                        and not body[-2].els and isinstance(body[-1], HBreak) and not _has_continue(body[:-2]):
                    s = HDoWhile(body[:-2], body[-2].cond)
                else:
                    s = HWhile(None, body)
            else:
                s = HWhile(s.cond, body)
        # for-loop: i = init; while (cond(i)) { ...; i = step(i); }
        if isinstance(s, HWhile) and s.cond is not None and res and isinstance(res[-1], HAssign) \
                and isinstance(res[-1].dest, Var) and s.body and isinstance(s.body[-1], HAssign):
            init, step = res[-1], s.body[-1]
            if step.dest == init.dest and init.dest.name in _vars_in(s.cond) \
                    and init.dest.name in _vars_in(step.src) and not _has_continue(s.body[:-1]):
                res.pop()
                s = HFor(init, s.cond, step, s.body[:-1])
        res.append(s)
    return res


# ---------------------------------------------------------------------------
# Public API
# ---------------------------------------------------------------------------

def decompile(mlil: MLILFunction, lifter=None) -> HLILFunction:
    """Structure an (non-SSA) MLIL function into HLIL."""
    if not mlil.blocks:
        return HLILFunction(mlil.name, (), [])
    _WIDTHS.clear(); _WIDTHS.update(mlil.widths)
    return HLILFunction(mlil.name, mlil.params, Structurer(mlil, lifter).run())


_TOKEN_RE = re.compile(r"""
    (?P<comment>/\*.*?\*/)
  | (?P<number>0x[0-9a-fA-F]+|\d+)
  | (?P<ident>[A-Za-z_][A-Za-z_0-9]*(?:\#\d+)?)
  | (?P<op><<|>>|<=|>=|==|!=|&&|\|\||->|[-+*/%&|^~!<>=?:.])
  | (?P<punct>[(){}\[\];,])
  | (?P<ws>\s+)
""", re.X)
_KEYWORDS = {"if", "else", "while", "for", "do", "return", "break", "continue", "goto", "true", "false"}


@dataclass
class Token:
    kind: str     # keyword | ident | number | op | punct | comment
    text: str
    line: int
    col: int


def tokenize(source) -> List[Token]:
    """Tokenise rendered HLIL (a string or an ``HLILFunction``)."""
    text = str(source)
    toks: List[Token] = []
    for ln, line in enumerate(text.split("\n"), 1):
        pos = 0
        while pos < len(line):
            m = _TOKEN_RE.match(line, pos)
            if not m:
                pos += 1; continue
            kind = m.lastgroup
            if kind != "ws":
                t = m.group()
                if kind == "ident" and t in _KEYWORDS: kind = "keyword"
                toks.append(Token(kind, t, ln, pos))
            pos = m.end()
    return toks


@dataclass
class BlockInfo:
    kind: str          # function | if | else | while | do | for
    header: str
    depth: int
    start_line: int    # 1-based, inclusive
    end_line: int


def extract_blocks(hlil: HLILFunction) -> List[BlockInfo]:
    """Return every brace-delimited structural block with its line span."""
    lines = hlil.lines()
    out: List[BlockInfo] = []
    stack: List[BlockInfo] = []
    pending: Optional[Line] = None
    for n, l in enumerate(lines, 1):
        if l.kind in ("header", "if", "else", "while", "do", "for"):
            pending = l
        elif l.kind == "open":
            kind = pending.kind if pending else "block"
            kind = "function" if kind == "header" else kind
            bi = BlockInfo(kind, pending.text if pending else "", l.depth, n, n)
            stack.append(bi); pending = None
        elif l.kind == "close" and stack:
            bi = stack.pop(); bi.end_line = n; out.append(bi)
    return sorted(out, key=lambda b: b.start_line)
