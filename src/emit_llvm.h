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
 * `if` lowers to a real `select` instruction, not a branch/phi pair — both branches in this v0's
 * own narrow, side-effect-free scope are pure scalar computations, so `select` is the exact
 * correct, simpler real choice (matches `emit_java.c`'s own ternary-based `if` lowering in
 * spirit, just LLVM's own SSA-native equivalent), avoiding basic-block splitting entirely.
 *
 * Real, honest, named semantic gap: `and`/`or` lower to LLVM's own plain `and`/`or` instructions
 * (bitwise on `i1`), which are NOT short-circuiting — every other backend's own `&&`/`||` (C/TS/
 * Java) IS short-circuiting. For this v0's own pure, side-effect-free scalar scope this is
 * observably identical (no side effect could ever be skipped), but it's a real, structural
 * difference named here rather than silently assumed equivalent — a future stateful/effectful
 * PARENA feature would need real short-circuit branches here, not attempted in this v0.
 *
 * A call to another top-level defn ASSUMES (does not independently re-verify) that its own real
 * return type matches the calling expression's own real type context — a real, narrow, honest v0
 * limitation named directly, not hidden: this file's own `LlvmFnSig` table records each defn's
 * declared return type precisely so a call site emits the CORRECT `call <type> @name(...)`
 * instruction, but does not independently re-check argument types against the callee's own
 * declared parameter types the way a real type checker would.
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
