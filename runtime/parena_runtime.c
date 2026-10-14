/* parena_runtime.c — see parena_runtime.h's own header comment for why
 * this is a separate implementation from src/arena.c rather than a
 * reuse of it, despite the identical bump-allocator mechanics. */
#include "parena_runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef PARENA_ARENA_BLOCK_MIN_CAPACITY
#define PARENA_ARENA_BLOCK_MIN_CAPACITY (64 * 1024)
#endif

#ifdef PARENA_NO_MALLOC
#ifndef PARENA_STATIC_POOL_BYTES
#define PARENA_STATIC_POOL_BYTES (1024u * 1024u)
#endif
#ifndef PARENA_STATIC_BLOCK_BYTES
#define PARENA_STATIC_BLOCK_BYTES (8u * 1024u)
#endif
#define PARENA_STATIC_NBLOCKS (PARENA_STATIC_POOL_BYTES / PARENA_STATIC_BLOCK_BYTES)
/* Static-pool mode serves blocks of this size by default (instead of the 64 KiB malloc-mode
 * minimum) so the working set of a small arena stays within L1/L2. */
#undef PARENA_ARENA_BLOCK_MIN_CAPACITY
#define PARENA_ARENA_BLOCK_MIN_CAPACITY (PARENA_STATIC_BLOCK_BYTES - 64u)

static unsigned char g_pool[PARENA_STATIC_POOL_BYTES] __attribute__((aligned(64)));
static unsigned char g_pool_run[PARENA_STATIC_NBLOCKS]; /* run length at run start, else 0 */
static unsigned char g_pool_used[PARENA_STATIC_NBLOCKS];
static size_t g_pool_in_use;
static size_t g_pool_high;

static void *static_alloc(size_t bytes) {
    size_t need = (bytes + PARENA_STATIC_BLOCK_BYTES - 1u) / PARENA_STATIC_BLOCK_BYTES;
    size_t i, run = 0;
    if (need == 0 || need > 255u) return NULL; /* run length is stored in a byte */
    for (i = 0; i < PARENA_STATIC_NBLOCKS; i++) {
        if (g_pool_used[i]) { run = 0; continue; }
        if (++run == need) {
            size_t first = i + 1u - need, j;
            for (j = first; j <= i; j++) g_pool_used[j] = 1;
            g_pool_run[first] = (unsigned char)need;
            g_pool_in_use += need * PARENA_STATIC_BLOCK_BYTES;
            if (g_pool_in_use > g_pool_high) g_pool_high = g_pool_in_use;
            return g_pool + first * PARENA_STATIC_BLOCK_BYTES;
        }
    }
    return NULL;
}

static void static_free(void *p, size_t bytes) {
    size_t first = (size_t)((unsigned char *)p - g_pool) / PARENA_STATIC_BLOCK_BYTES, n, j;
    (void)bytes;
    n = g_pool_run[first];
    for (j = first; j < first + n; j++) g_pool_used[j] = 0;
    g_pool_run[first] = 0;
    g_pool_in_use -= n * PARENA_STATIC_BLOCK_BYTES;
}
static ParenaBlockAllocFn g_block_alloc = static_alloc;
static ParenaBlockFreeFn g_block_free = static_free;
#else
static void *heap_alloc(size_t bytes) { return malloc(bytes); }
static void heap_free(void *p, size_t bytes) { (void)bytes; free(p); }
static ParenaBlockAllocFn g_block_alloc = heap_alloc;
static ParenaBlockFreeFn g_block_free = heap_free;
#endif

void parena_set_block_provider(ParenaBlockAllocFn alloc_fn, ParenaBlockFreeFn free_fn) {
    g_block_alloc = alloc_fn;
    g_block_free = free_fn;
}

size_t parena_static_pool_used(void) {
#ifdef PARENA_NO_MALLOC
    return g_pool_in_use;
#else
    return 0;
#endif
}

size_t parena_static_pool_high_water(void) {
#ifdef PARENA_NO_MALLOC
    return g_pool_high;
#else
    return 0;
#endif
}

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
    ParenaArenaBlock *b = (ParenaArenaBlock *)g_block_alloc(sizeof(ParenaArenaBlock) + capacity);
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

/* Over-allocates by (align - 8) at worst; blocks are 64-byte aligned so the common case
 * (fresh block, or a 64-multiple `used`) wastes nothing. */
void *arena_alloc_aligned(Arena *a, size_t size, size_t align) {
    size_t aligned_size = align_up(size);
    size_t pad;
    if (align < 8u) align = 8u;
    if (!a->head) { arena_alloc(a, 8u); a->head->used = 0; }
    pad = (align - (a->head->used & (align - 1u))) & (align - 1u);
    if (a->head->used + pad + aligned_size > a->head->capacity) {
        ParenaArenaBlock *b = arena_new_block(aligned_size);
        b->next = a->head;
        a->head = b;
        pad = 0;
    }
    a->head->used += pad;
    {
        void *ptr = a->head->data + a->head->used;
        a->head->used += aligned_size;
        return ptr;
    }
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
        g_block_free(b, sizeof(ParenaArenaBlock) + b->capacity);
        b = next;
    }
    a->head = NULL;
}
