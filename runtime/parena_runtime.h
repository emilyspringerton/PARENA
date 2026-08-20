/* parena_runtime.h — the minimal C runtime VS0-emitted programs link
 * against. Deliberately a separate file from src/arena.h, even though
 * the bump-allocator mechanics are identical: src/arena.h's own header
 * comment explicitly distinguishes "the compiler's own implementation-
 * language (C) memory management" from "Parena-the-language's own
 * :region/scratch and :region/buffer regions (a target-language
 * concept)" -- this file IS that target-language concept's real C
 * representation, so it gets its own identity rather than reusing the
 * compiler-internal one, honoring that documented boundary.
 */
#ifndef PARENA_RUNTIME_H
#define PARENA_RUNTIME_H

#include <stddef.h>

typedef struct ParenaArenaBlock {
    struct ParenaArenaBlock *next;
    size_t used;
    size_t capacity;
    unsigned char data[];
} ParenaArenaBlock;

typedef struct {
    ParenaArenaBlock *head;
} Arena;

void arena_init(Arena *a);
void *arena_alloc(Arena *a, size_t size);
char *arena_strdup(Arena *a, const char *src, size_t len);

/* arena_free_all — also used directly as a GCC/Clang cleanup attribute
 * function (`Arena x __attribute__((cleanup(arena_free_all)));`), which
 * is exactly why its signature is `void (Arena *)`, matching what the
 * cleanup attribute requires without a wrapper. This is the real C
 * emission target for every `with-arena` block: the arena is torn down
 * automatically at the end of its own C block scope, mirroring
 * NORTHSTAR.md's own memory-model description verbatim ("reclaimed
 * when its region ends"). */
void arena_free_all(Arena *a);

/* Result/Option — the real C representation VS0's own `match` emission
 * targets. NORTHSTAR.md's own "Zero-allocation pattern matching" section
 * names `Option T`/`Result T E` as core, `match`-destructured tagged
 * unions -- this is their real C shape: `tag` distinguishes Ok/Some
 * (1) from Err/None (0), `value` carries the payload as `void *`. Real,
 * honest limitation: one shared `void *value` field for both variants
 * (rather than a real, separately-typed union) is a genuine loss of
 * C-level type safety, matching VS0's own already-stated "no function-
 * signature table / full type-checking pass yet" gap elsewhere in this
 * emitter -- not pretended solved here either. */
typedef struct {
    int tag; /* 1 = Ok, 0 = Err */
    void *value;
} Result;

typedef struct {
    int tag; /* 1 = Some, 0 = None */
    void *value;
} Option;

static inline Result result_ok(void *v) {
    Result r;
    r.tag = 1;
    r.value = v;
    return r;
}
static inline Result result_err(void *v) {
    Result r;
    r.tag = 0;
    r.value = v;
    return r;
}
static inline Option option_some(void *v) {
    Option o;
    o.tag = 1;
    o.value = v;
    return o;
}
static inline Option option_none(void) {
    Option o;
    o.tag = 0;
    o.value = NULL;
    return o;
}

#endif /* PARENA_RUNTIME_H */
