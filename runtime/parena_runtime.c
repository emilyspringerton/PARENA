/* parena_runtime.c — see parena_runtime.h's own header comment for why
 * this is a separate implementation from src/arena.c rather than a
 * reuse of it, despite the identical bump-allocator mechanics. */
#include "parena_runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PARENA_ARENA_BLOCK_MIN_CAPACITY (64 * 1024)

void arena_init(Arena *a) {
    a->head = NULL;
}

static size_t align_up(size_t n) {
    return (n + 7u) & ~(size_t)7u;
}

static ParenaArenaBlock *arena_new_block(size_t min_capacity) {
    size_t capacity = min_capacity > PARENA_ARENA_BLOCK_MIN_CAPACITY
                           ? min_capacity
                           : PARENA_ARENA_BLOCK_MIN_CAPACITY;
    ParenaArenaBlock *b = (ParenaArenaBlock *)malloc(sizeof(ParenaArenaBlock) + capacity);
    if (!b) {
        fprintf(stderr, "parena runtime: out of memory (arena block alloc failed)\n");
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
        ParenaArenaBlock *b = arena_new_block(aligned);
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
    ParenaArenaBlock *b = a->head;
    while (b) {
        ParenaArenaBlock *next = b->next;
        free(b);
        b = next;
    }
    a->head = NULL;
}
