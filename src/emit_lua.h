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
 *   * `/` lowers to `//`, Luau's floor division, because PARENA's `/` on I32 is integer division -- emitting `/`
 *     would silently produce floats and every parity test between the two worlds would fail for no visible reason.
 *     Caveat, named: C truncates toward zero and `//` floors, so the two agree only for non-negative operands,
 *     which is the only case the shared rules use (a divider's operand is a count or a duration).
 *   * `if` becomes `(cond and then or else)`. Correct for every integer/string branch this ABI produces; it would
 *     be wrong for a `false` branch value, so the emitter REFUSES nothing but the header says it: do not write a
 *     shared rule whose `then` branch is a bare boolean false.
 *   * names are camelCased (kebab `ambient-pick` -> `ambientPick`), matching the hand-written Luau module this
 *     target exists to replace.
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
