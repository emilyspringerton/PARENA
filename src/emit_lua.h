/* emit_lua.h -- real, v0, narrow-scope Lua/Luau emitter.
 *
 * Founder real-time (2026-10-30): "build the shankpit affordances into NOOBTOPIA including combat and slippery
 * physics PARENA IS THE GLUE make it so PARENA can emitlua or whatever it should be trivial".
 *
 * It is trivial in exactly the way the TypeScript and Java targets already proved: PARENA's rules are pure
 * integer functions, and every one of them is a statement-free expression tree. So the Lua target is the same
 * narrow slice as emit_java.c -- scalar params, ONE body expression, the same small operator table -- emitted as
 * a Lua TABLE MODULE:
 *
 *     local M = {}
 *     function M.categoryForMood(mood)
 *         return ...
 *     end
 *     return M
 *
 * which is what a Roblox script can `require()` and a plain interpreter can also load. That shape matters for the
 * three-way merge: SHANKPIT calls the C emitted from the SAME .prn file, NOOBTOPIA calls this, and neither keeps
 * its own copy of a threshold.
 *
 * Real, target-specific differences, spelled out rather than discovered later:
 *   * `=` lowers to `==` (Lua's `=` is assignment, never equality).
 *   * `and`/`or`/`not` are already Lua.
 *   * `/` lowers to the module-local `__idiv(a, b)`, which truncates toward zero like C (Lua's floor division
 *     disagrees on every negative dividend). It is written with `math.floor`, not `//`, so the output also loads in
 *     Lua 5.1 -- World of Warcraft's dialect, where `//` is a syntax error (2026-11-13).
 *   * `if` becomes an immediately-invoked function `(function() if c then return a else return b end end)()`,
 *     not `(c and a or b)`, which is wrong whenever the taken branch's value is false (see emit_lua.c).
 *   * string literals are re-escaped for Lua (`\"`, `\\`, `\n`, `\r`, `\t`, `\ddd` for other control bytes);
 *     UTF-8 passes through unchanged, which the 12 WoW client locales rely on.
 *   * the `lua/` bridge (spec Step 2, 2026-11-13), named after emit_ts.c's `js/` family: `lua/global`,
 *     `lua/defined?`, `lua/call-fn`, `lua/call` (method call with `:`), `lua/get`, `lua/aget` (raw, 1-based),
 *     `lua/obj`, `lua/array`, `lua/nil?`, plus `str`. This is how a REFLUX mod reaches WoW's API globals and
 *     tables. An unknown `lua/` form is refused by name.
 *   * names are camelCased (kebab `ambient-pick` -> `ambientPick`), matching the hand-written Luau module this
 *     target exists to replace.
 *   * `let` lowers to an immediately-invoked function: `(let [a .. b ..] body)` -> `(function() local a = ..;
 *     local b = ..; return body end)()`. Lua has no expression-local binding, and an IIFE keeps the form an
 *     EXPRESSION (so it still nests inside the `(cond and then or else)` an `if` becomes) while `local`s keep
 *     `let`'s rule that each binding sees the earlier ones. Added when the first real policy module was pointed at
 *     this target (DEADWEIGHT's citizen policy, 2026-11-03) -- before it, the binding vector fell through to the
 *     call branch and the emitter reported the vector as an unsupported expression.
 *   * statement forms this target cannot express in expression position (`do`, `set!`, loops, `def`/`defstruct`/
 *     `defenum`, `match`, `case`) are REFUSED BY NAME. Without that list they fell through to the "call to another
 *     defn" branch and emitted `M.do(1, 2)` -- plausible-looking Lua that only a real interpreter rejects, i.e.
 *     the emitter reporting success on output that cannot run. Found by this target's own test.
 *
 * What is still NOT supported, honestly: those statement forms, multiple body expressions, structs/enums/Vec, and
 * region-annotated parameters. A module that needs them is a real gap, not a silent one -- `emit_lua` returns NULL
 * and names the form.
 *
 * Deliberately independent of emit.c/emit_ts.c/emit_java.c, same "each target module stays independent" rule
 * those three already follow.
 */
#ifndef PARENA_EMIT_LUA_H
#define PARENA_EMIT_LUA_H

#include "arena.h"
#include "ast.h"

/* Emits `program` as one Lua module returning a table of functions. Returns an arena-owned string with *out_error
 * NULL on success; on the first construct it does not understand it returns NULL and sets *out_error to a message
 * naming the unsupported form, never partial or guessed-at Lua. */
const char *emit_lua(Arena *arena, Node *program, const char **out_error);

#endif /* PARENA_EMIT_LUA_H */
