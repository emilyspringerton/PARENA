#include "arena.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ARENA_BLOCK_MIN_CAPACITY (64 * 1024)

void arena_init(Arena *a) {
    a->head = NULL;
}

static size_t align_up(size_t n) {
    return (n + 7u) & ~(size_t)7u;
}

static ArenaBlock *arena_new_block(size_t min_capacity) {
    size_t capacity = min_capacity > ARENA_BLOCK_MIN_CAPACITY ? min_capacity : ARENA_BLOCK_MIN_CAPACITY;
    ArenaBlock *b = (ArenaBlock *)malloc(sizeof(ArenaBlock) + capacity);
    if (!b) {
        /* A failed malloc here means the compiler itself is out of memory --
           nothing meaningful to recover into, matches this codebase's own
           fail-loudly convention elsewhere (e.g. REDGARDEN's ticket-secret
           fail-closed checks) rather than returning a null AST node a
           caller might not check. */
        fprintf(stderr, "parena: out of memory (arena block alloc failed)\n");
        exit(1);
    }
    b->next = NULL;
    b->used = 0;
    b->capacity = capacity;
    return b;
}

void *arena_alloc(Arena *a, size_t size) {
    size_t aligned = align_up(size);
    if (!a->head || a->head->used + aligned > a->head->capacity) {
        ArenaBlock *b = arena_new_block(aligned);
        b->next = a->head;
        a->head = b;
    }
    void *ptr = a->head->data + a->head->used;
    a->head->used += aligned;
    return ptr;
}

char *arena_strdup(Arena *a, const char *src, size_t len) {
    char *dst = (char *)arena_alloc(a, len + 1);
    memcpy(dst, src, len);
    dst[len] = '\0';
    return dst;
}

void arena_free_all(Arena *a) {
    ArenaBlock *b = a->head;
    while (b) {
        ArenaBlock *next = b->next;
        free(b);
        b = next;
    }
    a->head = NULL;
}
