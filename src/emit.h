/* emit.h — VS0 Definition of Done, domain 3: the C99 emitter.
 * NORTHSTAR.md's own DoD table: "C99 emitter using __attribute__
 * ((cleanup)) for with-arena forms."
 *
 * Real, honest scope, same discipline as region.h's own header: this
 * emits correct C99 for exactly the shape `examples/test.prn`'s own
 * valid function (`load-config`) uses -- function params typed `Arena
 * @ region`, `with-arena` blocks, `let` bindings built from `(alloc
 * arena-expr String "literal")`, and a trailing symbol as the return
 * value. It is not yet a general-purpose emitter for arbitrary future
 * `.prn` programs (no numeric/boolean literals, no arithmetic, no
 * nested function calls beyond `alloc`, no multi-type inference beyond
 * `char *` for String allocations) -- real, separate follow-up work,
 * flagged here rather than silently claimed as done. Callers are
 * expected to have already run region_analyze() and rejected any
 * error before calling emit_c(); this does not re-check region safety.
 */
#ifndef PARENA_EMIT_H
#define PARENA_EMIT_H

#include "arena.h"
#include "ast.h"

/* emit_c walks every top-level (defn ...) form in `program` and
 * produces a complete, compilable C99 translation unit (including the
 * #include of parena_runtime.h). Returns an arena-owned C string on
 * success with *out_error set to NULL. On the first construct it
 * doesn't know how to emit, returns NULL and sets *out_error to an
 * arena-owned message naming the unsupported form -- never emits
 * partial or guessed-at C for something it doesn't actually understand. */
const char *emit_c(Arena *arena, Node *program, const char **out_error);

#endif /* PARENA_EMIT_H */
