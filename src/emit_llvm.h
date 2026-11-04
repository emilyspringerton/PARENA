/* emit_llvm.h — real, v0, narrow-scope LLVM IR emitter (2026-09-10, founder real-time: "continue
 * working on LLVM we want to do both plans first the clang rout then the direct AVR route start
 * the clang work" — this is that "direct AVR route": PARENA's OWN compiler emitting real LLVM IR
 * text directly, not shelling out to a C compiler at all for the decision-logic portion. See
 * `docs/LLVM_BACKEND_NORTHSTAR.md`'s own "(2) — the literal ask" section for the full context this
 * closes: unlike (1) (using clang instead of gcc as the external C compiler, Phase 0/1, already
 * shipped), this is the version that actually makes `parena` itself "support LLVM directly."
 *
 * Real, honest, narrow v0 scope — the SAME real narrow slice `emit_ts.h`/`emit_java.h` already
 * document (not repeated in full here): a `defn` with zero or more scalar (I32/F64/Bool/String)
 * parameters, no Arena/region annotations, a body that is a single real expression. String support
 * (added 2026-09-10, same day as the rest of this file) lowers to LLVM's opaque `ptr` type — a
 * literal becomes a real, private, module-level global constant
 * (`@.str.N = private unnamed_addr constant [LEN x i8] c"...\00"`, hex-escaped per LLVM's own real
 * `c"..."` syntax, NUL-terminated matching this repo's own established "C-string-shaped String"
 * convention), referenced directly by the global's own name — no `getelementptr` decay needed,
 * since LLVM's opaque pointers (default since LLVM 14+) mean a global array's own name already
 * IS a plain `ptr` value, verified live against real `llc 18`, not assumed from older
 * typed-pointer-era LLVM IR examples. Still no Vec/Result/Region/pattern-matching in this v0 —
 * real, separate, not-yet-attempted scope.
 *
 * Real, LLVM-specific structural difference from every other emitter in this repo (the genuinely
 * new part, not a mechanical find-replace): LLVM IR is SSA-register-based, not a nested-expression
 * text language like C/TypeScript/Java. `(+ a b)` in C/TS/Java is just the text `(a + b)`; in LLVM
 * IR it's a real, separate instruction line (`%3 = add i32 %a, %b`) producing a fresh named
 * register, referenced by that name afterward — so this emitter, unlike emit_ts.c/emit_java.c,
 * accumulates a real list of instructions per function alongside computing each expression's own
 * result register, rather than building one flat text expression.
 *
 * Real, honest, narrow type-inference approach: PARENA's own source has no numeric-literal type
 * suffix (`60` could mean I32 or F64 depending on context), so a real "expected type" is threaded
 * top-down ONLY to disambiguate bare number literals — every other expression (symbol references,
 * binop results, comparison results, calls, `not`) computes its OWN real type bottom-up from its
 * own structure (a symbol's declared parameter type, a comparison's own real `i1` result type
 * regardless of its operands' type, a call's own declared return type), then the two are checked
 * against each other where they meet (e.g. both `if` branches, both binop operands) — a genuine,
 * real type-mismatch is a real, honest compile error, never silently-wrong emitted IR.
 *
 * `if` lowers to a real `select` instruction when both arms are SPECULABLE, and to real basic
 * blocks when they are not. "Speculable" used to be spelled "pure", which was wrong: a division
 * has no effects but can trap, so `(if (<= b 0) 0 (/ a b))` used to be lowered to a `select` that
 * evaluated both arms — correct under `src/emit.c`, `RuntimeError: divide by zero` under this
 * emitter on wasm32 (found live 2026-11-04, fixed: `head_may_trap`). Effects and trapping are two
 * separate questions, and `if` promises only one arm runs.
 *
 * Real, honest, named semantic gap: `and`/`or` lower to LLVM's own plain `and`/`or` instructions
 * (bitwise on `i1`), which are NOT short-circuiting — every other backend's own `&&`/`||` (C/TS/
 * Java) IS short-circuiting. For a side-effect-free scalar operand this is observably identical,
 * but trapping operands are the counter-example (see src/emit_llvm.c's own comment at the and/or
 * case): `(and (> b 0) (> (/ a b) 2))` traps here at b = 0 and would not in a short-circuiting
 * backend. Named, not silently assumed equivalent; real short-circuit branches are a separate item.
 *
 * A call to another top-level defn's own `LlvmFnSig` (2026-09-10, real follow-up) now records
 * every declared parameter's own type, not just the return type — a real call site independently
 * checks both argument COUNT and each argument's own real type against the callee's declared
 * signature, a real, honest compile error on either mismatch rather than a silently malformed
 * `call` instruction. This also means a bare numeric literal argument now correctly picks up the
 * callee's own declared parameter type as its `expected_type` hint, closing what was previously a
 * real, separate "no type context" failure for that shape.
 */
#ifndef PARENA_EMIT_LLVM_H
#define PARENA_EMIT_LLVM_H

#include "arena.h"
#include "ast.h"

/* emit_llvm walks every top-level (defn ...) form in `program` and produces one real LLVM IR
 * module (`.ll` text) — each defn becomes one real `define <ret_type> @<name>(<params>) { ... }`.
 * Returns an arena-owned string on success with *out_error set to NULL. On the first construct it
 * doesn't know how to emit, returns NULL and sets *out_error to an arena-owned message naming the
 * unsupported form — never emits partial or guessed-at IR, same real discipline every other
 * emitter in this repo already establishes. */
const char *emit_llvm(Arena *arena, Node *program, const char **out_error);

#endif /* PARENA_EMIT_LLVM_H */
