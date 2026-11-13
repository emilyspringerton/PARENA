/* tests/test_u32_ops.c -- the u32-* word forms emit well-defined C: wrap-around, logical shifts, rotations,
 * unsigned compare and the high product word, at the edges where the signed operators are undefined. Built
 * with -fsanitize=undefined where the toolchain has it, so an emitted signed overflow fails the run. */
#include <stdio.h>
#include <stdint.h>
#include "test_u32_ops_gen.c"

static int failures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { printf("FAIL: %s\n", msg); failures++; } else { printf("PASS: %s\n", msg); } } while (0)
#define U(x) ((int)(uint32_t)(x))

int main(void) {
    CHECK(t_add(U(0xffffffffu), 1) == 0, "add wraps 0xffffffff + 1 = 0");
    CHECK(t_add(U(0x7fffffffu), 1) == U(0x80000000u), "add crosses the sign bit without UB");
    CHECK(t_sub(0, 1) == U(0xffffffffu), "sub wraps 0 - 1");
    CHECK(t_mul(U(0x10001u), U(0x10001u)) == U(0x20001u), "mul keeps the low 32 bits");
    CHECK(t_mulhi(U(0xffffffffu), U(0xffffffffu)) == U(0xfffffffeu), "mulhi of max*max");
    CHECK(t_mulhi(U(0x10001u), U(0x10001u)) == 1, "mulhi 0x10001^2");
    CHECK(t_shl(1, 31) == U(0x80000000u), "shl into the top bit");
    CHECK(t_shl(U(0xffffffffu), 32) == U(0xffffffffu), "shl count is taken mod 32");
    CHECK(t_ushr(U(0x80000000u), 31) == 1, "ushr shifts zeros in");
    CHECK(t_ushr(-1, 28) == 15, "ushr of -1");
    CHECK(t_rotr(U(0x00000001u), 1) == U(0x80000000u), "rotr 1");
    CHECK(t_rotr(U(0x12345678u), 0) == U(0x12345678u), "rotr 0 is identity");
    CHECK(t_rotr(U(0x12345678u), 8) == U(0x78123456u), "rotr 8");
    CHECK(t_rotl(U(0x12345678u), 8) == U(0x34567812u), "rotl 8");
    CHECK(t_rotl(U(0x80000000u), 1) == 1, "rotl carries the top bit round");
    CHECK(t_lt(1, U(0x80000000u)) == 1 && t_lt(U(0x80000000u), 1) == 0, "lt is unsigned");
    CHECK(t_lt(5, 5) == 0, "lt is strict");
    printf("%s: %d failure(s)\n", failures ? "FAIL" : "OK", failures);
    return failures ? 1 : 0;
}
