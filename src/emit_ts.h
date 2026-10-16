/* emit_ts.h — real, v0, narrow-scope TypeScript emitter (founder real-time, 2026-08-30:
 * "start working on the parena ts emitter using MISHRI as proving ground").
 *
 * Real, honest scope, matching emit.h's own original documented precedent for VS0's C emitter
 * ("this emits correct C99 for exactly the shape... not yet a general-purpose emitter" -- narrow
 * on purpose, grown later via real, incremental, gap-driven expansion, same discipline STDLIB.md
 * documents dozens of times over for the C backend): this emits correct TypeScript for exactly
 * the shape a "mods first everything" scalar decision function already uses everywhere in this
 * monorepo (PAPERCRAFT's own xp_award_mod.prn/phone_mod.prn/item_drop_mod.prn/inventory_mod.prn/
 * pickup_mod.prn) -- a `defn` with zero or more scalar (I32/F64/Bool) parameters, no Arena/region
 * annotations at all (TypeScript is garbage-collected; there is no region system to translate),
 * and a body that is a SINGLE, real expression: number/symbol literals, the arithmetic/
 * comparison/logical binops the C emitter already recognizes, `if` as an expression (ternary,
 * not a statement -- no `let`/block support yet), and calls to either another top-level `defn` in
 * the same file or one, real, recognized external primitive this v0 knows how to lower to a real
 * host call (`math/random` -> `Math.random()`, the one real FFI-shaped gap MISHRI's own real
 * `HumannessLayer.bezierInterp` needs and PARENA's own stdlib has no `math` package for yet --
 * see stdlib/math/random.prn's own doc comment).
 *
 *
 * 2026-10-16 (BRAWLPIT web; founder: "whatever we do need to use js for use parena into TS") the v0
 * scope above was grown, still gap by gap, into a host-code emitter:
 *   statements   let (block-scoped, typed, `(name : Type)` binders), do, when, if (ternary when pure,
 *                statement otherwise), loop/recur (while(true)+continue, tail position only), set!,
 *                dotimes / cond / case / unless / inc! ... (ast_sugar.h, shared with the wasm backend),
 *                multi-form defn bodies
 *   module       (def name init) -> a module-level `let`; (start expr) -> run when the module loads
 *   types        I32 F64 Bool String Unit and `Any`, an opaque JavaScript value
 *   js bridge    (js/global "name") (js/get o "p") (js/aget a i) (js/set! o "p" v) (js/aset! a i v)
 *                (js/call o "m" args..) (js/call-fn f args..) (js/new "C" args..) (js/obj "k" v ..)
 *                (js/array ..) (js/null? x) (js/typeof x) (js/defined? "g") (str a b ..) and
 *                (callback fn) -- a named top-level defn as the closure (no lambdas in PARENA)
 *   operators    + - * / (I32 / truncates) mod = != < > <= >= and or not bit-and/or/xor shl shr ushr
 * A statement form where a value is needed, or an undeclared symbol, is a compile error rather than
 * silently wrong TypeScript. Output for scalar-expression functions is unchanged from v0.
 *
 * Deliberately NOT shared code with emit.c's own C backend -- a real, separate, independent
 * module (its own minimal string-builder, its own minimal name-mangling), not a refactor of the
 * existing, hard-won, CI-green C emitter into a multi-target abstraction. That refactor is real,
 * separate, much bigger, much riskier work (STDLIB.md's own commit history is dozens of real,
 * individually-found-and-fixed C-emitter gaps deep) -- not attempted here, and not needed for
 * this real v0 slice to be genuinely useful on its own.
 */
#ifndef PARENA_EMIT_TS_H
#define PARENA_EMIT_TS_H

#include "arena.h"
#include "ast.h"

/* emit_ts walks every top-level (defn ...) form in `program` and produces a complete TypeScript
 * source file. Returns an arena-owned string on success with *out_error set to NULL. On the
 * first construct it doesn't know how to emit, returns NULL and sets *out_error to an
 * arena-owned message naming the unsupported form -- never emits partial or guessed-at
 * TypeScript for something it doesn't actually understand, same real discipline emit_c already
 * established. */
const char *emit_ts(Arena *arena, Node *program, const char **out_error);

#endif /* PARENA_EMIT_TS_H */
