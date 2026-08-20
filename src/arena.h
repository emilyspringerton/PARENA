/* arena.h — the compiler's own bump allocator for AST nodes and lexer-owned
 * strings. NORTHSTAR.md's VS0 scope: "Parser: S-expression reader in C using
 * a single bump arena for compiler AST nodes." No individual frees; the
 * whole arena is torn down at once when the compiler process exits or a
 * fresh compile begins. Not the same thing as Parena-the-language's own
 * :region/scratch and :region/buffer regions (those are a target-language
 * concept the region analyzer reasons about) — this is purely the
 * compiler's own implementation-language (C) memory management.
 */
#ifndef PARENA_ARENA_H
#define PARENA_ARENA_H

#include <stddef.h>

typedef struct ArenaBlock {
    struct ArenaBlock *next;
    size_t used;
    size_t capacity;
    unsigned char data[];
} ArenaBlock;

typedef struct {
    ArenaBlock *head; /* most-recently-allocated block; new blocks prepend */
} Arena;

/* arena_init: zero-initializes an Arena. No allocation happens until the
 * first arena_alloc call (lazy first block). */
void arena_init(Arena *a);

/* arena_alloc: bump-allocates `size` bytes, 8-byte aligned, from the arena.
 * Allocates a new block (>= size, plus block-management overhead) if the
 * current block doesn't have room — a single bump arena, per the
 * NORTHSTAR's own wording, doesn't mean literally one malloc call ever;
 * it means "no per-node free," which this satisfies regardless of block
 * count. */
void *arena_alloc(Arena *a, size_t size);

/* arena_strdup: copies `len` bytes from `src` into a fresh arena
 * allocation and NUL-terminates it. Used for token text (symbols,
 * keywords, string literals) so the AST doesn't hold pointers into a
 * source buffer that might not outlive parsing. */
char *arena_strdup(Arena *a, const char *src, size_t len);

/* arena_free_all: releases every block. Only ever called at process exit
 * or before a fresh compile in the test harness — never mid-parse. */
void arena_free_all(Arena *a);

#endif /* PARENA_ARENA_H */
