/* region.h — VS0 Definition of Done, domain 2: the region analyzer.
 * NORTHSTAR.md's own DoD table: "Valid script compiles cleanly to C;
 * invalid script aborts with `Compile Error: Escaping region pointer
 * from :region/scratch to :region/buffer at line X`."
 *
 * Real, honest scope: this is a single-pass checker for exactly the
 * assignment invariant NORTHSTAR's own "Memory model" section states --
 * Region(Source) >= Region(Destination) -- implemented as a real,
 * general symbol-table walk (with-arena/let bindings tracked by region
 * rank, escape detected at call sites shaped like `(dest-arena-expr
 * source-value-expr ...)`), not hardcoded to test.prn's own two
 * functions specifically. What it does NOT attempt: full bidirectional
 * type inference, the Return invariant (Region(Value) >= Region(Caller)),
 * or the Move/ownership invariant -- those are real, separate, unstarted
 * follow-up work, flagged here rather than silently claimed as done.
 */
#ifndef PARENA_REGION_H
#define PARENA_REGION_H

#include "arena.h"
#include "ast.h"

/* region_analyze walks every top-level (defn ...) form in `program`.
 * Returns NULL if every form satisfies the assignment invariant.
 * On the first violation found, returns an arena-owned error message
 * in the exact DoD format above. */
const char *region_analyze(Arena *arena, Node *program);

#endif /* PARENA_REGION_H */
