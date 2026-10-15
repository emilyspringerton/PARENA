#!/usr/bin/env python3
"""Run the tests/v16/cases corpus against the V16 engine compiled to WebAssembly.

usage: run_wasm.py build/v16.wasm tests/v16/cases [script.js ...]

Needs the `wasmtime` python package (pip install wasmtime). The module must have ZERO imports --
that is part of what this checks: the engine is self-contained, an embedder needs no WASI shim.
Each .js case is executed in a fresh instance AND the whole corpus is replayed inside ONE instance
(arena reset between runs) to prove runs do not leak into each other.
"""
import os
import sys

try:
    import wasmtime
except ImportError:
    print("SKIP: python 'wasmtime' package not installed (pip install wasmtime)")
    sys.exit(0)


class V16:
    def __init__(self, path):
        self.engine = wasmtime.Engine()
        self.module = wasmtime.Module.from_file(self.engine, path)
        if self.module.imports:
            raise SystemExit("FAIL: module has imports: %r" % [(i.module, i.name) for i in self.module.imports])
        self.store = wasmtime.Store(self.engine)
        inst = wasmtime.Instance(self.store, self.module, [])
        self.ex = inst.exports(self.store)
        if "_initialize" in self.ex:
            self.ex["_initialize"](self.store)
        self.mem = self.ex["memory"]

    def run(self, src: bytes) -> str:
        cap = self.ex["v16_input_capacity"](self.store)
        assert len(src) <= cap, "script larger than the module's input buffer"
        self.mem.write(self.store, src, self.ex["v16_input"](self.store))
        n = self.ex["v16_run"](self.store, len(src))
        ptr = self.ex["v16_output"](self.store)
        return bytes(self.mem.read(self.store, ptr, ptr + n)).decode("utf-8", "replace")


def main():
    wasm, cases = sys.argv[1], sys.argv[2]
    names = sorted(f[:-3] for f in os.listdir(cases) if f.endswith(".js"))
    failed = 0
    for name in names:
        src = open(os.path.join(cases, name + ".js"), "rb").read()
        want = open(os.path.join(cases, name + ".out"), "rb").read().decode()
        got = V16(wasm).run(src)  # fresh instance per case
        if got == want:
            print("PASS (wasm, fresh instance): %s" % name)
        else:
            failed += 1
            print("FAIL (wasm): %s\n--- want ---\n%s--- got ---\n%s------------" % (name, want, got))
    shared = V16(wasm)
    for name in names:  # one instance, many runs: the arena is reset between them
        src = open(os.path.join(cases, name + ".js"), "rb").read()
        want = open(os.path.join(cases, name + ".out"), "rb").read().decode()
        if shared.run(src) != want:
            failed += 1
            print("FAIL (wasm, shared instance): %s" % name)
    print("wasm module: %d bytes, 0 imports, %d cases x2 (fresh + shared instance), %d failed"
          % (os.path.getsize(wasm), len(names), failed))
    sys.exit(1 if failed else 0)


main()
