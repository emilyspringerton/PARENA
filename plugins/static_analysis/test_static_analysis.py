"""Unit tests for the PARENA static-analysis IR pipeline.

Run from the repo root:   python3 -m unittest plugins.static_analysis.test_static_analysis -v
(or `python3 -m pytest plugins/static_analysis`).  Pure stdlib; the gcc-based
tests are skipped when no C compiler is available.
"""
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
from collections import Counter

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..")))

from plugins.static_analysis import (  # noqa: E402
    Lifter, UnsupportedBinary, constant_propagate, eliminate_dead_code, extract_blocks,
    optimize, tokenize, to_ssa,
)
from plugins.static_analysis.lifter import (  # noqa: E402
    Const, LSetFlag, LSetReg, LStore, MCall, MPhi, MRet, MSet, Var,
)

BASE = 0x1000


def lift(hexstr: str):
    """Lift a raw x86-64 blob; returns the single function at BASE."""
    lf = Lifter.from_bytes(bytes.fromhex(hexstr.replace(" ", "")), base=BASE)
    return lf, lf.functions[BASE]


# -- hand-assembled snippets ---------------------------------------------------
# mov eax,5 ; add eax,3 ; imul eax,eax,2 ; ret
ARITH = "b8 05000000  83 c0 03  6b c0 02  c3"
# mov ebx,99 ; mov ebx,1 ; mov eax,ebx ; ret          (first write is dead)
DEAD = "bb 63000000  bb 01000000  89 d8  c3"
# cmp edi,0 ; jle L ; mov eax,1 ; jmp E ; L: mov eax,2 ; E: ret   (diamond -> phi)
DIAMOND = "83 ff 00  7e 07  b8 01000000  eb 05  b8 02000000  c3"
# xor eax,eax ; xor ecx,ecx ; L: cmp ecx,edi ; jge E ; add eax,ecx ; inc ecx ; jmp L ; E: ret
LOOP = "31 c0  31 c9  39 f9  7d 06  01 c8  ff c1  eb f6  c3"
# mov eax,1 ; cmp eax,1 ; je T ; mov eax,7 ; T: ret         (branch is constant-true)
CONSTBR = "b8 01000000  83 f8 01  74 05  b8 07000000  c3"
# push rbp ; mov rbp,rsp ; mov dword [rbp-4],41 ; mov eax,[rbp-4] ; add eax,1 ; pop rbp ; ret
FRAME = "55  48 89 e5  c7 45 fc 29000000  8b 45 fc  83 c0 01  5d  c3"


class TestLLIL(unittest.TestCase):
    def test_flags_and_stack_effects_are_explicit(self):
        _, f = lift("55 48 89 e5 83 ff 00 5d c3")  # push rbp; mov rbp,rsp; cmp edi,0; pop rbp; ret
        stmts = f.llil.insns
        # push rbp -> rsp = rsp - 8 ; [rsp] = rbp
        self.assertIsInstance(stmts[0], LSetReg)
        self.assertEqual((stmts[0].reg, str(stmts[0].src)), ("rsp", "rsp - 8"))
        self.assertIsInstance(stmts[1], LStore)
        # cmp writes every status flag as its own node
        flags = {s.flag for s in stmts if isinstance(s, LSetFlag)}
        self.assertTrue({"zf", "sf", "cf", "of", "pf"} <= flags)
        # ret pops the return address explicitly
        self.assertTrue(any(isinstance(s, LSetReg) and s.reg == "rsp" and str(s.src) == "rsp + 8"
                            for s in stmts[-3:]))

    def test_32bit_write_zero_extends_and_8bit_merges(self):
        _, f = lift("b8 ff ff ff ff  b0 01  c3")  # mov eax,-1 ; mov al,1 ; ret
        a, b = f.llil.insns[0], f.llil.insns[1]
        self.assertEqual(str(a), "rax = (uint32_t)0xffffffff")
        self.assertIn("rax &", str(b))              # partial write merges with old rax

    def test_unknown_opcode_is_reported_not_guessed(self):
        _, f = lift("06 c3")  # invalid in 64-bit mode
        self.assertTrue(any(type(s).__name__ == "LTrap" for s in f.llil.insns))


class TestMLIL(unittest.TestCase):
    def test_frame_slots_become_named_variables(self):
        _, f = lift(FRAME)
        text = str(f.mlil)
        self.assertIn("var_c = ", text)             # [rbp-4] with return addr + saved rbp pushed
        self.assertNotIn("rsp", text)               # stack-pointer bookkeeping absorbed

    def test_flags_fold_into_conditions(self):
        _, f = lift(DIAMOND)
        cond = f.mlil.blocks[0].insns[-1].cond
        self.assertEqual(str(cond), "(int32_t)rdi <= 0")


class TestSSA(unittest.TestCase):
    def assert_single_assignment(self, ssa):
        written = Counter()
        for ins in ssa.instructions():
            for v in ins.vars_written:
                if v.version is not None:
                    written[v] += 1
        self.assertTrue(written)
        self.assertEqual([v for v, n in written.items() if n != 1], [])
        self.assertEqual(ssa.check(), [])

    def test_variables_assigned_exactly_once_straight_line(self):
        _, f = lift(ARITH)
        self.assert_single_assignment(f.ssa)
        rax_versions = sorted(v.version for v in f.ssa.defs if v.name == "rax")
        self.assertEqual(rax_versions, [1, 2, 3])   # each update spawns a new subscript

    def test_phi_at_merge_point(self):
        _, f = lift(DIAMOND)
        ssa = f.ssa
        self.assert_single_assignment(ssa)
        merge = ssa.blocks[-1]
        phis = [i for i in merge.insns if isinstance(i, MPhi) and i.dest.name == "rax"]
        self.assertEqual(len(phis), 1)
        phi = phis[0]
        self.assertEqual(len(phi.srcs), 2)
        self.assertEqual(len({v.version for v in phi.srcs.values()}), 2)
        # the return consumes the phi result
        ret = merge.insns[-1]
        self.assertIsInstance(ret, MRet)
        self.assertEqual(ret.vars_read, (phi.dest,))

    def test_loop_has_header_phis_and_back_edge(self):
        _, f = lift(LOOP)
        ssa = f.ssa
        self.assert_single_assignment(ssa)
        header = next(b for b in ssa.blocks if len(b.preds) == 2)
        phi_names = {i.dest.name for i in header.insns if isinstance(i, MPhi)}
        self.assertTrue({"rax", "rcx"} <= phi_names)
        phi = next(i for i in header.insns if isinstance(i, MPhi) and i.dest.name == "rcx")
        back = max(phi.srcs)                         # source from the latch block
        self.assertGreater(phi.srcs[back].version, phi.srcs[min(phi.srcs)].version)

    def test_def_use_lookup(self):
        _, f = lift(ARITH)
        ssa = f.ssa
        v1, v2 = Var("rax", 1), Var("rax", 2)
        d = ssa.def_of(v2)
        self.assertEqual(d.vars_written, (v2,))
        self.assertIn(v1, d.vars_read)               # rax#2 = f(rax#1)
        self.assertIn(d, ssa.uses_of(v1))
        self.assertEqual(ssa.def_of(v1).dest, v1)
        # reading a never-defined register yields version 0 (live-in)
        _, g = lift(DIAMOND)
        self.assertIn(Var("rdi", 0), g.ssa.live_in)

    def test_ssa_does_not_mutate_source_mlil(self):
        _, f = lift(DIAMOND)
        before = str(f.mlil)
        to_ssa(f.mlil)
        self.assertEqual(before, str(f.mlil))


class TestOptimizations(unittest.TestCase):
    def test_constant_folding_resolves_arithmetic(self):
        _, f = lift(ARITH)
        ssa = to_ssa(f.mlil)
        self.assertTrue(constant_propagate(ssa))
        ret = ssa.blocks[0].insns[-1]
        self.assertEqual(ret.value, Const(16))       # (5 + 3) * 2
        optimize(ssa)
        self.assertEqual([str(i) for i in ssa.blocks[0].insns], ["return 0x10"])

    def test_folding_wraps_to_operand_width(self):
        # mov eax,0xffffffff ; add eax,2 ; ret  -> 32-bit wrap == 1
        _, f = lift("b8 ff ff ff ff  83 c0 02  c3")
        ssa = optimize(to_ssa(f.mlil))
        self.assertEqual(ssa.blocks[0].insns[-1].value, Const(1))

    def test_folding_through_stack_slot(self):
        _, f = lift(FRAME)
        ssa = optimize(to_ssa(f.mlil))
        self.assertEqual(ssa.blocks[0].insns[-1].value, Const(42))
        self.assertEqual(str(f.hlil).count("return 0x2a"), 1)

    def test_signed_and_division_folding(self):
        # mov eax,-7 ; cdq ; mov ecx,2 ; idiv ecx ; ret   -> -3 (truncating)
        _, f = lift("b8 f9 ff ff ff  99  b9 02 00 00 00  f7 f9  c3")
        ssa = optimize(to_ssa(f.mlil))
        self.assertEqual(ssa.blocks[0].insns[-1].value, Const(0xFFFFFFFD))

    def test_dead_code_elimination(self):
        _, f = lift(DEAD)
        ssa = to_ssa(f.mlil)
        before = [str(i) for i in ssa.blocks[0].insns]
        self.assertIn("rbx#1 = 0x63", before)
        self.assertTrue(eliminate_dead_code(ssa))
        after = "\n".join(str(i) for i in ssa.instructions())
        self.assertNotIn("0x63", after)              # the overwritten 99 is gone
        self.assertEqual(ssa.check(), [])
        # every surviving definition is consumed (or has an effect)
        for v, d in ssa.defs.items():
            if isinstance(d, (MSet, MPhi)):
                self.assertTrue(ssa.uses_of(v), f"{v} defined but unused")

    def test_dead_code_keeps_side_effects(self):
        # mov dword [rdi],5 ; ret   -- store must survive, nothing reads it
        _, f = lift("c7 07 05 00 00 00  c3")
        ssa = optimize(to_ssa(f.mlil))
        self.assertTrue(any(type(i).__name__ == "MStore" for i in ssa.instructions()))

    def test_constant_branch_is_folded_and_dead_arm_removed(self):
        _, f = lift(CONSTBR)
        ssa = to_ssa(f.mlil)
        n_before = len(ssa.blocks)
        optimize(ssa)
        self.assertLess(len(ssa.blocks), n_before)
        self.assertEqual(ssa.check(), [])
        self.assertEqual("\n".join(str(i) for i in ssa.instructions()), "return 1")
        self.assertNotIn("7", str(f.hlil).replace("0x", ""))

    def test_optimized_ssa_stays_valid_on_loops(self):
        _, f = lift(LOOP)
        ssa = optimize(to_ssa(f.mlil))
        self.assertEqual(ssa.check(), [])
        self.assertTrue(any(isinstance(i, MPhi) for i in ssa.instructions()))  # loop-carried


class TestHLIL(unittest.TestCase):
    def test_if_else(self):
        _, f = lift(DIAMOND)
        text = str(f.hlil)
        self.assertIn("if (", text)
        self.assertIn("else", text)
        self.assertEqual(text.count("return"), 1)

    def test_for_loop_reconstructed(self):
        _, f = lift(LOOP)
        text = str(f.hlil)
        self.assertIn("for (", text)
        self.assertNotIn("goto", text)
        kinds = [b.kind for b in extract_blocks(f.hlil)]
        self.assertEqual(kinds.count("for"), 1)
        self.assertEqual(kinds[0], "function")

    def test_tokenizer(self):
        _, f = lift(LOOP)
        toks = tokenize(f.hlil)
        self.assertIn(("keyword", "for"), [(t.kind, t.text) for t in toks])
        self.assertIn(("keyword", "return"), [(t.kind, t.text) for t in toks])
        self.assertTrue(all(t.line >= 1 for t in toks))

    def test_array_and_member_access(self):
        # mov eax,[rdi+rsi*4] ; ret      and      mov eax,[rdi+8] ; ret
        _, f = lift("8b 04 b7  c3")
        self.assertIn("rdi[rsi]", str(f.hlil))
        _, g = lift("8b 47 08  c3")
        self.assertIn("rdi->field_8", str(g.hlil))


# -- container formats ----------------------------------------------------------

def build_elf(code: bytes, sym: str = "answer", vaddr: int = 0x401000, machine: int = 62) -> bytes:
    shstr = b"\0.text\0.symtab\0.strtab\0.shstrtab\0"
    strtab = b"\0" + sym.encode() + b"\0"
    symtab = b"\0" * 24 + struct.pack("<IBBHQQ", 1, 0x12, 0, 1, vaddr, len(code))
    off_text = 0x1000
    off_sym = off_text + len(code)
    off_str = off_sym + len(symtab)
    off_shs = off_str + len(strtab)
    off_sh = (off_shs + len(shstr) + 7) & ~7
    def sh(name, typ, flags, addr, off, size, link=0, info=0, align=1, ent=0):
        return struct.pack("<IIQQQQIIQQ", name, typ, flags, addr, off, size, link, info, align, ent)
    shdrs = (sh(0, 0, 0, 0, 0, 0) +
             sh(1, 1, 6, vaddr, off_text, len(code), align=16) +
             sh(7, 2, 0, 0, off_sym, len(symtab), link=3, info=1, align=8, ent=24) +
             sh(15, 3, 0, 0, off_str, len(strtab)) +
             sh(23, 3, 0, 0, off_shs, len(shstr)))
    ehdr = b"\x7fELF" + bytes([2, 1, 1, 0]) + b"\0" * 8 + struct.pack(
        "<HHIQQQIHHHHHH", 2, machine, 1, vaddr, 64, off_sh, 0, 64, 56, 1, 64, 5, 4)
    phdr = struct.pack("<IIQQQQQQ", 1, 5, off_text, vaddr, vaddr, len(code), len(code), 0x1000)
    img = bytearray(ehdr + phdr)
    img += b"\0" * (off_text - len(img)) + code + symtab + strtab + shstr
    img += b"\0" * (off_sh - len(img)) + shdrs
    return bytes(img)


def build_pe(machine: int = 0x8664) -> bytes:
    image_base, text_rva, raw = 0x140000000, 0x1000, 0x200
    sect = bytearray(0x200)
    sect[0:7] = b"\xff\x15" + struct.pack("<i", 0x160 - 6) + b"\xc3"   # call [rip+IAT]; ret
    sect[0x100:0x114] = struct.pack("<IIIII", 0x1140, 0, 0, 0x1180, 0x1160)
    sect[0x140:0x148] = struct.pack("<Q", 0x11A0)
    sect[0x160:0x168] = struct.pack("<Q", 0x11A0)
    sect[0x180:0x18D] = b"kernel32.dll\0"
    sect[0x1A0:0x1AE] = b"\0\0ExitProcess\0"
    dos = bytearray(64); dos[:2] = b"MZ"; struct.pack_into("<I", dos, 0x3C, 0x40)
    coff = struct.pack("<HHIIIHH", machine, 1, 0, 0, 0, 240, 0x22)
    opt = bytearray(240)
    struct.pack_into("<H", opt, 0, 0x20B)
    struct.pack_into("<I", opt, 16, text_rva)
    struct.pack_into("<Q", opt, 24, image_base)
    struct.pack_into("<II", opt, 32, 0x1000, 0x200)
    struct.pack_into("<II", opt, 56, 0x2000, 0x200)
    struct.pack_into("<I", opt, 108, 16)
    struct.pack_into("<II", opt, 112 + 8, 0x1100, 40)                    # import directory
    secthdr = b".text\0\0\0" + struct.pack("<IIIIIIHHI", 0x200, text_rva, 0x200, raw, 0, 0, 0, 0, 0x60000020)
    hdr = bytes(dos) + b"PE\0\0" + coff + bytes(opt) + secthdr
    return hdr + b"\0" * (raw - len(hdr)) + bytes(sect)


class TestContainers(unittest.TestCase):
    def test_raw(self):
        lf, f = lift(ARITH)
        self.assertEqual(lf.binary.fmt, "raw")
        self.assertEqual(f.start, BASE)

    def test_elf_symbols_and_entry(self):
        data = build_elf(bytes.fromhex(ARITH.replace(" ", "")))
        lf = Lifter.from_bytes(data)
        self.assertEqual(lf.binary.fmt, "elf")
        f = lf.get_function("answer")
        self.assertIsNotNone(f)
        self.assertEqual(f.start, 0x401000)
        self.assertIn("return 0x10", str(f.hlil))

    def test_elf_open_from_path(self):
        with tempfile.TemporaryDirectory() as d:
            p = os.path.join(d, "a.elf")
            with open(p, "wb") as fh:
                fh.write(build_elf(bytes.fromhex(LOOP.replace(" ", ""))))
            lf = Lifter.open(p)
            self.assertIn("for (", str(lf.get_function("answer").hlil))

    def test_pe_entry_and_imports(self):
        lf = Lifter.from_bytes(build_pe())
        self.assertEqual(lf.binary.fmt, "pe")
        f = lf.functions[0x140001000]
        self.assertIn("kernel32.dll!ExitProcess()", str(f.hlil))

    def test_wrong_architecture_rejected(self):
        with self.assertRaises(UnsupportedBinary):
            Lifter.from_bytes(build_elf(b"\xc3", machine=40))      # ARM
        with self.assertRaises(UnsupportedBinary):
            Lifter.from_bytes(build_pe(machine=0x1C0))


@unittest.skipUnless(shutil.which("gcc"), "gcc not available")
class TestGccBinaries(unittest.TestCase):
    SRC = r"""
int sum(int n){ int s=0; for(int i=0;i<n;i++) s+=i; return s; }
int fact(int n){ if(n<=1) return 1; return n*fact(n-1); }
int w(void){ int a=3; int b=a*4+2; return b-1; }
int arr(int *a,int n){ int t=0; int i=0; while(i<n){ t+=a[i]; i++; } return t; }
int main(void){ return sum(10)+fact(5)+w(); }
"""

    @classmethod
    def setUpClass(cls):
        cls.dir = tempfile.mkdtemp()
        src = os.path.join(cls.dir, "t.c")
        with open(src, "w") as fh:
            fh.write(cls.SRC)
        cls.bins = {}
        for opt in ("O0", "O2"):
            out = os.path.join(cls.dir, f"t_{opt}")
            subprocess.run(["gcc", f"-{opt}", "-fno-stack-protector", "-fcf-protection=none",
                            "-fno-pie", "-no-pie", "-o", out, src], check=True)
            cls.bins[opt] = Lifter.open(out)

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.dir, ignore_errors=True)

    def test_functions_indexed_by_symbol(self):
        names = {f.name for f in self.bins["O0"].functions.values()}
        self.assertTrue({"sum", "fact", "w", "arr", "main"} <= names)

    def test_every_function_yields_valid_ssa(self):
        for opt, lf in self.bins.items():
            for f in lf.functions.values():
                self.assertEqual(f.ssa.check(), [], f"{opt}:{f.name}")
                self.assertEqual(f.optimized_ssa.check(), [], f"{opt}:{f.name}")
                str(f.hlil)

    def test_constant_function(self):
        self.assertIn("return 0xd;", str(self.bins["O0"].get_function("w").hlil))

    def test_loop_and_recursion(self):
        s = str(self.bins["O0"].get_function("sum").hlil)
        self.assertIn("for (", s)
        self.assertIn("fact(", str(self.bins["O0"].get_function("fact").hlil))
        self.assertIn("[", str(self.bins["O0"].get_function("arr").hlil))

    def test_whole_program_folds_at_O2(self):
        # gcc -O2 inlines everything; sum(10)+fact(5)+w() = 45 + 120 + 13
        self.assertIn("return 0xb2;", str(self.bins["O2"].get_function("main").hlil))


if __name__ == "__main__":
    unittest.main()
