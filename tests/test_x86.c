/* tests/test_x86.c -- stdlib/re/x86.prn encoding-layout checks. With a file argument it also
 * linear-sweeps that raw byte file and prints "off len mnem" per instruction (used for the
 * differential check against plugins/static_analysis/lifter.py's decoder). */
#include "parena_runtime.h"
#include <stdio.h>
#include <string.h>
#include "test_x86_gen.c"

static int failures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { printf("FAIL: %s\n", msg); failures++; } \
    else { printf("PASS: %s\n", msg); } } while (0)

static int L(const char *b, int n) { return x86_length((char *)b, n, 0); }

int main(int argc, char **argv) {
    Arena a; arena_init(&a);
    if (argc > 1) {
        FILE *f = fopen(argv[1], "rb"); static char buf[1 << 20];
        int n = (int)fread(buf, 1, sizeof buf, f); fclose(f);
        int off = 0;
        while (off < n) {
            int l = x86_length(buf, n, off);
            if (l < 0) break;
            printf("%d %d %s\n", off, l, x86_mnemonic(buf, n, off));
            off += l;
        }
        return 0;
    }
    CHECK(L("\x55", 1) == 1, "push rbp");
    CHECK(L("\x48\x89\xe5", 3) == 3, "mov rbp,rsp");
    CHECK(L("\x48\x83\xec\x10", 4) == 4, "sub rsp,0x10 (imm8)");
    CHECK(L("\x48\x8b\x45\xf8", 4) == 4, "mov rax,[rbp-8] (disp8)");
    CHECK(L("\x8b\x04\x25\x00\x10\x00\x00", 7) == 7, "mov eax,[abs] (SIB no base, disp32)");
    CHECK(L("\x48\x8d\x05\x10\x00\x00\x00", 7) == 7, "lea rax,[rip+0x10]");
    CHECK(L("\xb8\x01\x00\x00\x00", 5) == 5, "mov eax,1");
    CHECK(L("\x48\xb8\x01\x02\x03\x04\x05\x06\x07\x08", 10) == 10, "movabs rax,imm64");
    CHECK(L("\x66\xb8\x01\x00", 4) == 4, "mov ax,1 (66 prefix, imm16)");
    CHECK(L("\xe8\x00\x00\x00\x00", 5) == 5, "call rel32");
    CHECK(L("\x0f\x84\x00\x00\x00\x00", 6) == 6, "jz rel32 (0F map)");
    CHECK(L("\xf3\x0f\x1e\xfa", 4) == 4, "endbr64");
    CHECK(L("\x48\x89", 2) == -1, "truncated -> -1");
    CHECK(L("\x0f\x58\xc1", 3) == -1, "SSE addps unsupported -> -1");
    CHECK(strcmp(x86_mnemonic("\x48\x83\xec\x10", 4, 0), "sub") == 0, "group 83 /5 = sub");
    CHECK(strcmp(x86_mnemonic("\xff\xd0", 2, 0), "call") == 0, "ff /2 = call");
    CHECK(x86_modrm_byte("\x48\x89\xe5", 3, 0) == 0xe5 && x86_opcode("\x48\x89\xe5", 3, 0) == 0x89,
          "field accessors");
    printf(failures ? "FAILED\n" : "ALL PASS\n");
    return failures != 0;
}
