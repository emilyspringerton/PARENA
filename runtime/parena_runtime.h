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

#endif /* PARENA_RUNTIME_H */
