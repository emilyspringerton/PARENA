"""SSA construction and def-use analysis over MLIL.

``to_ssa(mlil)`` returns an ``SSAFunction``: every variable has exactly one
definition per version (version 0 = live-in / parameter), φ-nodes (``MPhi``)
join values at CFG merges, and each instruction exposes ``vars_read`` /
``vars_written`` as versioned ``Var`` objects.  ``SSAFunction.defs`` and
``SSAFunction.uses`` give O(1) def/use lookup.

Optimisations (all operate on, and preserve, SSA form):
  * ``constant_propagate``  - constant propagation + folding, branch folding,
                              unreachable-block pruning
  * ``eliminate_dead_code`` - removes defs whose versions are never consumed
  * ``forward_substitute``  - inlines single-use temporaries inside a block
  * ``optimize``            - runs the three to a fixed point

Variables whose address is taken (``AddrOf``) are *not* versioned: they may be
modified through pointers, so they are left as plain memory-like variables.
"""
from __future__ import annotations

from collections import defaultdict
from typing import Dict, List, Optional, Set, Tuple

from .lifter import (
    SYSV_ARGS, AddrOf, Const, Expr, MCall, MGoto, MIf, MInsn, MLILBlock, MLILFunction,
    MPhi, MRet, MSet, MStore, MTrap, MUnimpl, Var, _collect_vars, expr_has_load, fold,
    subst_vars, terminator_succs,
)


# ---------------------------------------------------------------------------
# Graph helpers (shared with the structurer in decompiler.py)
# ---------------------------------------------------------------------------

def reverse_postorder(succs: Dict[int, List[int]], entry: int) -> List[int]:
    seen, order = set(), []
    stack = [(entry, iter(succs.get(entry, ())))]
    seen.add(entry)
    while stack:
        n, it = stack[-1]
        for s in it:
            if s not in seen:
                seen.add(s)
                stack.append((s, iter(succs.get(s, ()))))
                break
        else:
            order.append(n)
            stack.pop()
    return order[::-1]


def compute_idoms(succs: Dict[int, List[int]], entry: int) -> Dict[int, Optional[int]]:
    """Cooper-Harvey-Kennedy iterative dominators.  idom[entry] is None."""
    rpo = reverse_postorder(succs, entry)
    num = {n: i for i, n in enumerate(rpo)}
    preds: Dict[int, List[int]] = defaultdict(list)
    for n in rpo:
        for s in succs.get(n, ()):
            if s in num:
                preds[s].append(n)
    idom: Dict[int, Optional[int]] = {entry: entry}

    def intersect(a: int, b: int) -> int:
        while a != b:
            while num[a] > num[b]: a = idom[a]
            while num[b] > num[a]: b = idom[b]
        return a

    changed = True
    while changed:
        changed = False
        for n in rpo[1:]:
            ps = [p for p in preds[n] if p in idom]
            if not ps: continue
            new = ps[0]
            for p in ps[1:]:
                new = intersect(p, new)
            if idom.get(n) != new:
                idom[n] = new; changed = True
    idom[entry] = None
    return idom


def dominates(idom: Dict[int, Optional[int]], a: int, b: int) -> bool:
    while b is not None:
        if a == b: return True
        b = idom.get(b)
    return False


def dominance_frontiers(succs: Dict[int, List[int]], idom: Dict[int, Optional[int]]) -> Dict[int, Set[int]]:
    preds: Dict[int, List[int]] = defaultdict(list)
    for n, ss in succs.items():
        if n not in idom: continue
        for s in ss: preds[s].append(n)
    df: Dict[int, Set[int]] = {n: set() for n in idom}
    for n in idom:
        if len(preds[n]) < 2: continue
        for p in preds[n]:
            if p not in idom: continue
            r = p
            while r is not None and r != idom[n]:
                df[r].add(n)
                r = idom[r]
    return df


# ---------------------------------------------------------------------------
# Instruction rewriting
# ---------------------------------------------------------------------------

def map_instr(ins: MInsn, expr_fn, dest_fn=lambda v: v) -> MInsn:
    """Return a copy of ``ins`` with ``expr_fn`` applied to every read expression
    and ``dest_fn`` applied to every written variable."""
    if isinstance(ins, MSet): new = MSet(dest_fn(ins.dest), expr_fn(ins.src), ins.addr)
    elif isinstance(ins, MStore): new = MStore(expr_fn(ins.mem_addr), expr_fn(ins.src), ins.size, ins.addr)
    elif isinstance(ins, MCall):
        new = MCall(expr_fn(ins.target), [expr_fn(a) for a in ins.args], [dest_fn(o) for o in ins.outs], ins.addr)
    elif isinstance(ins, MRet): new = MRet(expr_fn(ins.value), ins.addr)
    elif isinstance(ins, MIf): new = MIf(expr_fn(ins.cond), ins.t, ins.f, ins.addr)
    elif isinstance(ins, MGoto): new = MGoto(ins.target, ins.addr)
    elif isinstance(ins, MTrap): new = MTrap(ins.kind, tuple(expr_fn(a) for a in ins.args), ins.addr)
    elif isinstance(ins, MUnimpl): new = MUnimpl(ins.text, ins.addr)
    elif isinstance(ins, MPhi):
        new = MPhi(dest_fn(ins.dest), {b: expr_fn(v) for b, v in ins.srcs.items()}, ins.addr)
    else:  # pragma: no cover
        raise TypeError(type(ins))
    new.block = ins.block
    return new


# ---------------------------------------------------------------------------
# SSA function
# ---------------------------------------------------------------------------

class SSAFunction:
    def __init__(self, name: str, blocks: List[MLILBlock], escaped: frozenset,
                 widths: Optional[Dict[str, int]] = None):
        self.name, self.blocks, self.escaped = name, blocks, escaped
        self.widths = dict(widths or {})
        self.defs: Dict[Var, MInsn] = {}
        self.uses: Dict[Var, List[MInsn]] = {}
        self.reindex()

    # -- def-use ------------------------------------------------------------
    def reindex(self) -> None:
        """Rebuild the def/use maps (one pass over the function)."""
        self.defs, self.uses = {}, defaultdict(list)
        for b in self.blocks:
            for ins in b.insns:
                ins.block = b.index
                for v in ins.vars_written:
                    if v.version is not None: self.defs[v] = ins
                for v in ins.vars_read:
                    if v.version is not None: self.uses[v].append(ins)

    def def_of(self, v: Var) -> Optional[MInsn]:
        return self.defs.get(v)

    def uses_of(self, v: Var) -> List[MInsn]:
        return self.uses.get(v, [])

    def instructions(self):
        for b in self.blocks:
            yield from b.insns

    @property
    def live_in(self) -> List[Var]:
        """Version-0 variables that are read: parameters / entry state."""
        seen = {}
        for ins in self.instructions():
            for v in ins.vars_read:
                if v.version == 0: seen.setdefault(v, None)
        return list(seen)

    def check(self) -> List[str]:
        """Validate SSA invariants; returns a list of violations (empty = valid)."""
        errs, defined = [], {}
        for b in self.blocks:
            for ins in b.insns:
                for v in ins.vars_written:
                    if v.version is None: continue
                    if v.version == 0: errs.append(f"{v} written with version 0")
                    if v in defined: errs.append(f"{v} assigned more than once")
                    defined[v] = ins
        for b in self.blocks:
            for ins in b.insns:
                for v in ins.vars_read:
                    if v.version not in (None, 0) and v not in defined:
                        errs.append(f"{v} read but never defined (B{b.index}: {ins})")
        return errs

    # -- views -------------------------------------------------------------------
    def to_mlil(self) -> MLILFunction:
        """Leave SSA: drop versions and φ-nodes.

        Sound here because the passes in this module never extend a live range
        past a redefinition of the same base variable (``forward_substitute``
        checks this), so all versions of a variable can share one name."""
        strip = lambda e: subst_vars(e, lambda v: Var(v.name))
        nb: List[MLILBlock] = []
        for b in self.blocks:
            out = []
            for ins in b.insns:
                if isinstance(ins, MPhi): continue
                new = map_instr(ins, strip, lambda v: Var(v.name))
                if isinstance(new, MSet) and new.src == new.dest: continue
                out.append(new)
            nblk = MLILBlock(b.index, b.start, out, list(b.preds))
            for i in out: i.block = b.index
            nb.append(nblk)
        names = {v.name for v in self.live_in}
        params = tuple([r for r in SYSV_ARGS if r in names] +
                       sorted(n for n in names if n.startswith("arg_")))
        return MLILFunction(self.name, nb, self.escaped, params, dict(self.widths))

    def __str__(self) -> str:
        out = [f"{self.name} (SSA):"]
        for b in self.blocks:
            out.append(f"  B{b.index} @ {b.start:#x}  preds={b.preds}")
            out += [f"    {i}" for i in b.insns]
        return "\n".join(out)


def _succ_map(blocks: List[MLILBlock]) -> Dict[int, List[int]]:
    return {b.index: terminator_succs(b.insns[-1]) if b.insns else [] for b in blocks}


# ---------------------------------------------------------------------------
# Construction
# ---------------------------------------------------------------------------

def to_ssa(mlil: MLILFunction) -> SSAFunction:
    """Compile an MLIL function into SSA form (semi-pruned Cytron et al.)."""
    blocks = mlil.blocks
    n = len(blocks)
    escaped = set(mlil.escaped)
    succs = _succ_map(blocks)
    preds: Dict[int, List[int]] = defaultdict(list)
    for b, ss in succs.items():
        for s in ss: preds[s].append(b)
    idom = compute_idoms(succs, 0)
    df = dominance_frontiers(succs, idom)

    # variables needing φs: written somewhere and read upward-exposed in some block
    def_blocks: Dict[str, Set[int]] = defaultdict(set)
    globals_: Set[str] = set()
    for b in blocks:
        killed: Set[str] = set()
        for ins in b.insns:
            for v in ins.vars_read:
                if v.name not in killed and v.name not in escaped: globals_.add(v.name)
            for v in ins.vars_written:
                if v.name in escaped: continue
                killed.add(v.name); def_blocks[v.name].add(b.index)
    phi_at: Dict[int, List[str]] = defaultdict(list)
    for name in sorted(globals_):
        work = list(def_blocks.get(name, ()))
        placed: Set[int] = set()
        while work:
            x = work.pop()
            for y in df.get(x, ()):
                if y not in placed:
                    placed.add(y); phi_at[y].append(name)
                    if y not in def_blocks[name]: work.append(y)

    new_blocks = [MLILBlock(b.index, b.start, [], list(preds.get(b.index, []))) for b in blocks]
    phis: Dict[Tuple[int, str], MPhi] = {}
    for bi, names in phi_at.items():
        for name in names:
            p = MPhi(Var(name), {})
            p.block = bi
            phis[(bi, name)] = p
            new_blocks[bi].insns.append(p)

    counter: Dict[str, int] = defaultdict(int)
    stacks: Dict[str, List[int]] = defaultdict(lambda: [0])

    def new_version(name: str) -> int:
        counter[name] += 1
        stacks[name].append(counter[name])
        return counter[name]

    def read_fn(v: Var) -> Var:
        return v if v.name in escaped else Var(v.name, stacks[v.name][-1])

    domtree: Dict[int, List[int]] = defaultdict(list)
    for node, d in idom.items():
        if d is not None: domtree[d].append(node)

    work: List[Tuple[str, int]] = [("enter", 0)]
    pushed: Dict[int, List[str]] = {}
    while work:
        kind, b = work.pop()
        if kind == "exit":
            for name in pushed.pop(b): stacks[name].pop()
            continue
        mine: List[str] = []
        for name in phi_at.get(b, ()):
            p = phis[(b, name)]
            p.dest = Var(name, new_version(name)); mine.append(name)
            p.__dict__.pop("vars_written", None); p.__dict__.pop("vars_read", None)
        for ins in blocks[b].insns:
            def dest_fn(v: Var) -> Var:
                if v.name in escaped: return v
                mine.append(v.name)
                return Var(v.name, new_version(v.name))
            # reads see versions from *before* this instruction's own writes
            new = map_instr(ins, lambda e: subst_vars(e, read_fn), lambda v: v)
            new = map_instr(new, lambda e: e, dest_fn)
            new.block = b
            new_blocks[b].insns.append(new)
        for s in succs[b]:
            for name in phi_at.get(s, ()):
                phis[(s, name)].srcs[b] = Var(name, stacks[name][-1])
        pushed[b] = mine
        work.append(("exit", b))
        for c in reversed(domtree.get(b, [])): work.append(("enter", c))

    # refresh cached read/write sets of φs (srcs were filled after creation)
    for bi, names in phi_at.items():
        for name in names:
            p = phis[(bi, name)]
            p.__dict__.pop("vars_read", None); p.__dict__.pop("vars_written", None)
    # blocks unreachable from the entry were never renamed; drop them
    keep = [b for b in new_blocks if b.index in idom]
    ssa = SSAFunction(mlil.name, keep, frozenset(escaped), mlil.widths)
    if len(keep) != n:
        _renumber(ssa)
    return ssa


def _renumber(ssa: SSAFunction) -> None:
    """Compact block indices after blocks were removed."""
    remap = {b.index: i for i, b in enumerate(ssa.blocks)}
    for b in ssa.blocks:
        b.index = remap[b.index]
        b.preds = [remap[p] for p in b.preds if p in remap]
        new = []
        for ins in b.insns:
            if isinstance(ins, MIf): ins = MIf(ins.cond, remap[ins.t], remap[ins.f], ins.addr)
            elif isinstance(ins, MGoto): ins = MGoto(remap[ins.target], ins.addr)
            elif isinstance(ins, MPhi):
                ins = MPhi(ins.dest, {remap[p]: v for p, v in ins.srcs.items() if p in remap}, ins.addr)
            ins.block = b.index
            new.append(ins)
        b.insns = new
    ssa.reindex()


# ---------------------------------------------------------------------------
# Optimisations
# ---------------------------------------------------------------------------

def _prune_unreachable(ssa: SSAFunction) -> bool:
    succs = _succ_map(ssa.blocks)
    reach = set(reverse_postorder(succs, 0))
    changed = len(reach) != len(ssa.blocks)
    for b in ssa.blocks:  # fix preds from actual terminators
        b.preds = []
    for b in ssa.blocks:
        if b.index not in reach: continue
        for s in succs[b.index]:
            if b.index not in ssa.blocks[s].preds: ssa.blocks[s].preds.append(b.index)
    # drop phi operands from removed/non-pred blocks; collapse single-source phis
    for b in ssa.blocks:
        if b.index not in reach: continue
        new = []
        for ins in b.insns:
            if isinstance(ins, MPhi):
                srcs = {p: v for p, v in ins.srcs.items() if p in b.preds}
                if len(srcs) != len(ins.srcs): changed = True
                if len(srcs) == 1:
                    ins = MSet(ins.dest, next(iter(srcs.values())), ins.addr); changed = True
                else:
                    ins = MPhi(ins.dest, srcs, ins.addr)
                ins.block = b.index
            new.append(ins)
        b.insns = new
    if len(reach) != len(ssa.blocks):
        ssa.blocks = [b for b in ssa.blocks if b.index in reach]
        _renumber(ssa)
    else:
        ssa.reindex()
    return changed


def _merge_blocks(ssa: SSAFunction) -> bool:
    """Fuse `A: ...; goto B` with B when A is B's only predecessor."""
    changed = False
    while True:
        for a in ssa.blocks:
            t = a.insns[-1] if a.insns else None
            if not isinstance(t, MGoto) or t.target == a.index: continue
            b = ssa.blocks[t.target]
            if b.index == 0 or b.preds != [a.index] or any(isinstance(i, MPhi) for i in b.insns):
                continue
            a.insns = a.insns[:-1] + b.insns
            for s in terminator_succs(b.insns[-1]):
                for i in ssa.blocks[s].insns:
                    if isinstance(i, MPhi) and b.index in i.srcs:
                        i.srcs[a.index] = i.srcs.pop(b.index)
                        i.__dict__.pop("vars_read", None)
            b.insns = []
            ssa.blocks = [x for x in ssa.blocks if x is not b]
            _renumber(ssa)
            for x in ssa.blocks: x.preds = []
            for x in ssa.blocks:
                for s in terminator_succs(x.insns[-1]):
                    if x.index not in ssa.blocks[s].preds: ssa.blocks[s].preds.append(x.index)
            changed = True
            break
        else:
            return changed


def constant_propagate(ssa: SSAFunction) -> bool:
    """Constant propagation + folding.  Returns True if anything changed."""
    any_change = False
    while True:
        env: Dict[Var, Const] = {}
        stable = False
        while not stable:
            stable = True
            for ins in ssa.instructions():
                if isinstance(ins, MSet) and ins.dest.version is not None and ins.dest not in env:
                    v = fold(subst_vars(ins.src, lambda x: env.get(x, x)))
                    if isinstance(v, Const): env[ins.dest] = v; stable = False
                elif isinstance(ins, MPhi) and ins.dest not in env:
                    vals = [env.get(s, s) if isinstance(s, Var) else s for s in ins.srcs.values()]
                    if vals and all(isinstance(v, Const) for v in vals) and len({v.value for v in vals}) == 1:
                        env[ins.dest] = vals[0]; stable = False
        changed = False
        for b in ssa.blocks:
            new = []
            for ins in b.insns:
                def ef(e, _env=env):
                    return fold(subst_vars(e, lambda x: _env.get(x, x)))
                # φ operands stay variables: a constant source would have no copy site
                # when leaving SSA (critical edges); only an all-equal φ collapses.
                cand = ins if isinstance(ins, MPhi) else map_instr(ins, ef)
                if isinstance(cand, MPhi):
                    srcs = {p: ef(v) for p, v in cand.srcs.items()}
                    if srcs and all(isinstance(v, Const) for v in srcs.values()) \
                            and len({v.value for v in srcs.values()}) == 1:
                        cand = MSet(cand.dest, next(iter(srcs.values())), cand.addr); cand.block = b.index
                if isinstance(cand, MIf) and isinstance(cand.cond, Const):
                    cand = MGoto(cand.t if cand.cond.value else cand.f, cand.addr); cand.block = b.index
                elif isinstance(cand, MIf) and cand.t == cand.f:
                    cand = MGoto(cand.t, cand.addr); cand.block = b.index
                if _differs(ins, cand): changed = True
                new.append(cand)
            b.insns = new
        ssa.reindex()
        pruned = _prune_unreachable(ssa)
        pruned |= _merge_blocks(ssa)
        any_change |= changed or pruned
        if not (changed or pruned):
            return any_change


def _differs(a: MInsn, b: MInsn) -> bool:
    return type(a) is not type(b) or str(a) != str(b)


def eliminate_dead_code(ssa: SSAFunction) -> bool:
    """Remove side-effect-free definitions whose versions are never read."""
    changed = False
    # a call's unused result variables are not outputs anyone depends on
    for b in ssa.blocks:
        for k, ins in enumerate(b.insns):
            if isinstance(ins, MCall):
                keep = [o for o in ins.outs if o.version is None or ssa.uses.get(o)]
                if len(keep) != len(ins.outs):
                    new = MCall(ins.target, ins.args, keep, ins.addr); new.block = b.index
                    b.insns[k] = new; changed = True
    ssa.reindex()
    count = {v: len(u) for v, u in ssa.uses.items()}
    removed: Set[int] = set()
    work = [i for i in ssa.instructions() if _removable(i, count)]
    while work:
        ins = work.pop()
        if id(ins) in removed or not _removable(ins, count): continue
        removed.add(id(ins))
        for v in ins.vars_read:
            if v.version is None: continue
            count[v] = count.get(v, 1) - 1
            d = ssa.defs.get(v)
            if d is not None and _removable(d, count): work.append(d)
    if removed:
        for b in ssa.blocks:
            b.insns = [i for i in b.insns if id(i) not in removed]
        ssa.reindex()
        changed = True
    return changed


def _removable(ins: MInsn, count: Dict[Var, int]) -> bool:
    if not isinstance(ins, (MSet, MPhi)): return False
    d = ins.dest
    return d.version is not None and count.get(d, 0) == 0


def forward_substitute(ssa: SSAFunction) -> bool:
    """Inline single-use definitions into their (same-block) use site."""
    changed = False
    for b in ssa.blocks:
        k = 0
        while k < len(b.insns):
            ins = b.insns[k]
            if not (isinstance(ins, MSet) and ins.dest.version is not None):
                k += 1; continue
            users = ssa.uses.get(ins.dest, [])
            if len(users) != 1 or users[0].block != b.index or isinstance(users[0], MPhi):
                k += 1; continue
            user = users[0]
            ui = next((j for j in range(k + 1, len(b.insns)) if b.insns[j] is user), None)
            if ui is None or user.vars_read.count(ins.dest) != 1:
                k += 1; continue
            src_vars = {v.name for v in ins.vars_read}
            mem = expr_has_load(ins.src) or any(n in ssa.escaped for n in src_vars)
            ok = True
            for mid in b.insns[k + 1:ui]:
                if any(w.name in src_vars for w in mid.vars_written): ok = False; break
                if mem and mid.has_side_effects: ok = False; break
            # the user itself may clobber a variable the expression reads *and*
            # evaluate it afterwards (MCall outs); reads happen first, so fine.
            if not ok:
                k += 1; continue
            sub = lambda e, _d=ins.dest, _s=ins.src: fold(subst_vars(e, lambda x: _s if x == _d else x))
            new = map_instr(user, sub)
            new.block = b.index
            b.insns[ui] = new
            del b.insns[k]
            ssa.reindex()
            changed = True
        # fallthrough
    return changed


def optimize(ssa: SSAFunction, max_rounds: int = 16) -> SSAFunction:
    """Run const-prop, DCE and forward substitution to a fixed point (in place)."""
    for _ in range(max_rounds):
        c = constant_propagate(ssa)
        d = eliminate_dead_code(ssa)
        f = forward_substitute(ssa)
        if not (c or d or f): break
    return ssa
