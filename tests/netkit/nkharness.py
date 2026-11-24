"""Test harness for netkit.wasm (PARENA -> LLVM -> wasm32, zero imports). Loads the module under wasmtime and gives
tests typed access to its linear memory and exports. Test-only code: nothing here ships."""
import os, struct
from wasmtime import Store, Module, Instance

G, SCR, PW, NAME, REASON, TXT, DEV, PORTS, OUT, IN, SSH, FB = (
    0x30000, 0x31000, 0x32000, 0x32100, 0x32200, 0x40000, 0x50000, 0x58000, 0x80000, 0x100000, 0x200000, 0x1000000)

class NK:
    def __init__(self, path):
        self.store = Store()
        self.module = Module.from_file(self.store.engine, path)
        assert not list(self.module.imports), "module must import nothing"
        self.inst = Instance(self.store, self.module, [])
        self.ex = self.inst.exports(self.store)
        self.memory = self.ex["memory"]
    def __getattr__(self, name):
        f = self.__dict__.get("ex", {}).get(name)
        if f is None: raise AttributeError(name)
        return lambda *a: f(self.store, *a)
    def mem(self): return self.memory.data_ptr(self.store)
    def size(self): return self.memory.data_len(self.store)
    def write(self, addr, data):
        import ctypes
        p = self.mem(); ctypes.memmove(ctypes.addressof(p.contents) + addr, bytes(data), len(data))
    def read(self, addr, n):
        import ctypes
        return ctypes.string_at(ctypes.addressof(self.mem().contents) + addr, n)
    def u32(self, addr): return struct.unpack("<I", self.read(addr, 4))[0]
    def g(self, k): return struct.unpack("<i", self.read(G + 4 * k, 4))[0]
    def cstr(self, addr, cap=4096):
        b = self.read(addr, cap); return b.split(b"\0", 1)[0].decode("utf-8", "replace")

_fails = 0; _passes = 0
def check(cond, msg):
    global _fails, _passes
    if cond: _passes += 1; print("PASS:", msg)
    else: _fails += 1; print("FAIL:", msg)
def done():
    print(f"\n{_passes} passed, {_fails} failed"); raise SystemExit(1 if _fails else 0)
