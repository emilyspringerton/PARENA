"""nk_garage.prn compiled by the real parena compiler to LLVM IR, and the IR EXECUTED here by a small interpreter (the IR subset the
garage uses: i32 arithmetic, icmp/select/br/call/alloca/load/store). This runs without the wasm toolchain, on the compiler's actual
output (not on a re-implementation); test_garage.py runs the same checks on the linked wasm. usage: test_garage_ir.py [parena]"""
import os, re, subprocess, sys, tempfile
from garage_checks import run
_f = _p = 0
def check(c, m):
    global _f, _p
    if c: _p += 1; print("PASS:", m)
    else: _f += 1; print("FAIL:", m)
def done():
    print(f"\n{_p} passed, {_f} failed"); raise SystemExit(1 if _f else 0)

root = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
parena = sys.argv[1] if len(sys.argv) > 1 else os.path.join(root, ".parena-bootstrap")
with tempfile.TemporaryDirectory() as w:
    ll = os.path.join(w, "g.ll")
    subprocess.run([parena, "build", os.path.join(root, "stdlib/netkit/nk_garage.prn"), "-o", ll], check=True, capture_output=True)
    text = open(ll).read()

strs, addr_of, nxt = {}, {}, 4096
for m in re.finditer(r'^(@\S+) = .*? c"(.*)"$', text, re.M):
    raw = re.sub(r"\\([0-9A-F]{2})", lambda x: chr(int(x.group(1), 16)), m.group(2))
    addr_of[m.group(1)] = nxt; strs[nxt] = raw.split("\0")[0]; nxt += len(raw) + 1
funcs = {}
for m in re.finditer(r"^define i32 @(\w+)\(([^)]*)\) \{\n(.*?)^\}", text, re.M | re.S):
    params = [p.split()[-1] for p in m.group(2).split(",") if p.strip()]
    blocks, cur = {}, None
    for line in m.group(3).splitlines():
        if re.match(r"^\w+:$", line): cur = line[:-1]; blocks[cur] = []
        elif line.strip(): blocks[cur].append(line.strip())
    funcs[m.group(1)] = (params, blocks)

def val(tok, env):
    tok = tok.strip().rstrip(",")
    if tok in addr_of: return addr_of[tok]
    return env[tok] if tok.startswith("%") else int(tok)
def s32(x): x &= 0xFFFFFFFF; return x - (1 << 32) if x >> 31 else x
CMP = {"eq": lambda a, b: a == b, "ne": lambda a, b: a != b, "slt": lambda a, b: a < b, "sle": lambda a, b: a <= b, "sgt": lambda a, b: a > b, "sge": lambda a, b: a >= b}
def call(name, *args):
    params, blocks = funcs[name]; env = dict(zip(params, args)); mem = {}; lab = "entry"
    while True:
        for ins in blocks[lab]:
            m = re.match(r"(%\S+) = alloca", ins)
            if m: continue
            m = re.match(r"store i32 (\S+), ptr (%\S+)", ins)
            if m: mem[m.group(2)] = val(m.group(1), env); continue
            m = re.match(r"(%\S+) = load i32, ptr (%\S+)", ins)
            if m: env[m.group(1)] = mem[m.group(2)]; continue
            m = re.match(r"(%\S+) = ptrtoint ptr (@\S+) to i32", ins)
            if m: env[m.group(1)] = addr_of[m.group(2)]; continue
            m = re.match(r"(%\S+) = icmp (\w+) i32 (\S+), (\S+)", ins)
            if m: env[m.group(1)] = int(CMP[m.group(2)](val(m.group(3), env), val(m.group(4), env))); continue
            m = re.match(r"(%\S+) = select i1 (\S+), i32 (\S+), i32 (\S+)", ins)
            if m: env[m.group(1)] = val(m.group(3), env) if val(m.group(2), env) else val(m.group(4), env); continue
            m = re.match(r"(%\S+) = (add|sub|mul|sdiv|srem) i32 (\S+), (\S+)", ins)
            if m:
                a, b = val(m.group(3), env), val(m.group(4), env)
                env[m.group(1)] = s32({"add": a + b, "sub": a - b, "mul": a * b, "sdiv": int(a / b) if b else 0, "srem": a - int(a / b) * b if b else 0}[m.group(2)]); continue
            m = re.match(r"(%\S+) = and i1 (\S+), (\S+)", ins)
            if m: env[m.group(1)] = val(m.group(2), env) & val(m.group(3), env); continue
            m = re.match(r"(%\S+) = or i1 (\S+), (\S+)", ins)
            if m: env[m.group(1)] = val(m.group(2), env) | val(m.group(3), env); continue
            m = re.match(r"(%\S+) = call i32 @(\w+)\((.*)\)", ins)
            if m: env[m.group(1)] = call(m.group(2), *[val(a.split()[-1], env) for a in m.group(3).split(",") if a.strip()]); continue
            m = re.match(r"ret i32 (\S+)", ins)
            if m: return val(m.group(1), env)
            m = re.match(r"br i1 (\S+), label %(\w+), label %(\w+)", ins)
            if m: lab = m.group(2) if val(m.group(1), env) else m.group(3); break
            m = re.match(r"br label %(\w+)", ins)
            if m: lab = m.group(1); break
            raise SystemExit("unsupported IR in nk_garage: " + ins)

run(call, lambda a: strs[a], check)
done()
