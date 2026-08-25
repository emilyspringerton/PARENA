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
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

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

/* result_unwrap_check / option_unwrap_check -- the real runtime half of
 * VS0's `unwrap` (see emit.c's own `is_call_named(expr, "unwrap")`
 * handling for the full real reasoning): Rust's own well-known
 * `.unwrap()` semantics -- pass through unchanged on Ok/Some, abort
 * with a real stderr message on Err/None, rather than silently
 * dereferencing a NULL `.value` (undefined behavior) or a stale one.
 * Pass-through-by-value (not void) so the call site can chain `.value`
 * straight off the return, evaluating the checked expression exactly
 * once -- the same real reason g_box_helpers' own generated helpers
 * are real functions, not a GNU statement-expression (rejected under
 * this project's own `-pedantic -Werror` build). */
static inline Result result_unwrap_check(Result r) {
    if (!r.tag) {
        fprintf(stderr, "parena: unwrap called on an Err result\n");
        abort();
    }
    return r;
}
static inline Option option_unwrap_check(Option o) {
    if (!o.tag) {
        fprintf(stderr, "parena: unwrap called on a None option\n");
        abort();
    }
    return o;
}

/* OK / ERR -- the real no-payload success/failure shorthand a real,
 * multi-file convention in stdlib's own #target inline-c bodies already
 * assumes exists (gfd.prn, thread.prn, io.prn, sdl2.prn, editor/
 * buffer.prn, pentest/pcap.prn -- 18 real call sites across 6 files, the
 * common `some_c_call(...) == 0 ? OK : ERR` shape for a `Result Unit
 * SomeError`-typed function whose real payload is Unit, not a value
 * worth carrying). Missing until now -- caught by actually compiling
 * gfd.prn's own real emitted C with gcc rather than trusting that
 * `parena build`'s own success (it doesn't validate inline-c content,
 * by design -- that's the whole point of the FFI trust boundary) meant
 * the result was real, working C. Defined as macros, not `static
 * inline` functions like `result_ok`/`result_err` above, since they
 * need to be usable as bare ternary-branch expressions the way every
 * real call site above already writes them. */
#define OK (result_ok(NULL))
#define ERR (result_err(NULL))

/* Vec -- STDLIB.md's own "vec — no dependencies, generic over T" design
 * (new/push!/get/len), erased to `void *` items the same real, honest
 * way Result/Option erase their own payload above (no generics/real
 * type-checking pass yet). Deliberately arena-allocated, not
 * malloc/realloc'd: STDLIB.md's own `vec/new` signature takes a `dest :
 * Arena @ Region` for a real reason -- every other allocation in this
 * language ties its lifetime to a region, and a `Vec` growing via
 * malloc/free underneath a language with no manual free anywhere else
 * would be a real, silent exception to that model. Growing means
 * bump-allocating a fresh, larger backing array from the same arena and
 * copying the old items over -- the old block is simply abandoned
 * inside the arena (never freed individually, matching every other
 * bump-allocator tradeoff `arena_alloc` already makes elsewhere), not
 * reclaimed until the whole arena itself is. Function names below are
 * written to already match `mangle()`'s own real output for `vec/new`/
 * `vec/push!`/`vec/get`/`vec/len` (`/`s and the trailing `!` both become
 * `_`) -- emit_call()'s generic call path needs no special-casing to
 * reach these, the same way it reaches any other stdlib function. */
typedef struct {
    Arena *arena;
    void **items;
    size_t count;
    size_t capacity;
} Vec;

static inline Vec vec_new(Arena *dest) {
    Vec v;
    v.arena = dest;
    v.items = NULL;
    v.count = 0;
    v.capacity = 0;
    return v;
}

static inline void vec_push_(Vec *v, void *item) {
    if (v->count == v->capacity) {
        size_t new_cap = v->capacity == 0 ? 4 : v->capacity * 2;
        void **new_items = (void **)arena_alloc(v->arena, new_cap * sizeof(void *));
        for (size_t i = 0; i < v->count; i++) new_items[i] = v->items[i];
        v->items = new_items;
        v->capacity = new_cap;
    }
    v->items[v->count++] = item;
}

static inline void *vec_get(Vec *v, int idx) {
    if (idx < 0 || (size_t)idx >= v->count) return NULL;
    return v->items[idx];
}

static inline int vec_len(Vec *v) {
    return (int)v->count;
}

/* vec_set_at_ -- real, minimal index-assignment, found genuinely
 * missing (matching mangle()'s own real output for `vec-set-at!`)
 * while getting world.prn's own `set-height` to compile. STDLIB.md's
 * own vec section never designed a `set!` operation (only new/push!/
 * get/len) -- a real, honest gap in the design doc, not just the
 * implementation, closed here to match the real, already-written call
 * site. Same real, honest safety convention vec_get's own out-of-
 * bounds NULL return already has: silently does nothing on an
 * out-of-bounds index rather than writing past the backing array,
 * since there's no real error-reporting channel a `void`-returning
 * runtime function has to use here. */
static inline void vec_set_at_(Vec *v, int idx, void *value) {
    if (idx < 0 || (size_t)idx >= v->count) return;
    v->items[idx] = value;
}

/* vec_box_i32/vec_box_f64 -- real, minimal scalar boxing, found
 * genuinely necessary while getting world.prn's own real `Terrain`
 * (`heights : (Vec F64)`) to compile: `Vec` stores `void *` items,
 * which fits pointer-representable elements (String/struct pointers)
 * directly, but a raw scalar (I32/F64) needs somewhere real to live
 * before its ADDRESS can be stored as the item -- not a bit-boxing
 * trick (reinterpreting the scalar's own bits as a pointer value),
 * deliberately: world.prn's own real `get-height`
 * (`(deref (vec/get ...))`) already uses the exact same `deref`
 * idiom uniformly for scalar and struct-typed Vecs alike, so the
 * real, consistent fix is making a scalar Vec's own stored items
 * genuinely BE real pointers to real, arena-allocated cells (the
 * same shape struct-pointer items already have), not a special case
 * `deref` or `vec_get` need to know about. Allocates into the Vec's
 * own already-stored arena (set once, at `vec_new`) -- no new
 * parameter needed at any real call site. */
static inline void *vec_box_i32(Vec *v, int value) {
    int *cell = (int *)arena_alloc(v->arena, sizeof(int));
    *cell = value;
    return cell;
}
static inline void *vec_box_f64(Vec *v, double value) {
    double *cell = (double *)arena_alloc(v->arena, sizeof(double));
    *cell = value;
    return cell;
}

/* string_concat -- real, minimal `string/concat` implementation
 * (STDLIB.md's own "string" package design), found genuinely missing
 * (not just designed) while getting firefly.prn's own `skip` to
 * actually gcc-compile (it calls `(string/concat "SKIP: " reason
 * dest)`). Allocates a real, arena-backed buffer sized to both inputs'
 * combined length + a null terminator, copies both, returns a real
 * `char *`. Real, honest, narrow scope: exactly two strings + a
 * destination arena, matching the one real call site that surfaced
 * this gap -- not a variadic/N-argument concat. */
static inline char *string_concat(const char *a, const char *b, Arena *dest) {
    size_t la = strlen(a);
    size_t lb = strlen(b);
    char *out = (char *)arena_alloc(dest, la + lb + 1);
    memcpy(out, a, la);
    memcpy(out + la, b, lb);
    out[la + lb] = '\0';
    return out;
}

/* ---- stdlib/io.prn real host glue (2026-08-24) ----------------------
 * Raw POSIX fd-based primitives only -- every Result/Option/FileHandle
 * value io.prn itself constructs is built with ordinary PARENA syntax
 * (Ok/Err/{:field val}), not here. Real, honest, narrow scope: each
 * function below returns a plain scalar or string, no boxing -- see
 * io.prn's own header comment for the full reasoning on why the split
 * is drawn exactly here. */
static inline int raw_open_impl(const char *path, int mode_tag) {
    int flags;
    switch (mode_tag) {
        case 0: flags = O_RDONLY; break;                      /* Read */
        case 1: flags = O_WRONLY | O_CREAT | O_TRUNC; break;  /* Write */
        case 2: flags = O_WRONLY | O_CREAT | O_APPEND; break; /* Append */
        default: flags = O_RDONLY; break;
    }
    return open(path, flags, 0644);
}

static inline int raw_write_impl(int fd, const char *s) {
    size_t len = strlen(s);
    size_t written = 0;
    while (written < len) {
        ssize_t n = write(fd, s + written, len - written);
        if (n < 0) return -1;
        written += (size_t)n;
    }
    return 0;
}

/* raw_read_all_impl -- reads every remaining byte from fd (from its
 * current position) into one arena-allocated, NUL-terminated buffer.
 * Grows in fixed 4096-byte chunks -- real, honest, simple, matching
 * string_concat's own "narrow, not optimized" scope above. A read()
 * error partway through returns whatever was successfully read so far
 * rather than failing outright -- read-string's own real return type
 * has no way to report a mid-read error separately from a full one
 * without a second host primitive this file's own real scope doesn't
 * need yet. */
static inline char *raw_read_all_impl(int fd, Arena *dest) {
    size_t cap = 4096;
    size_t len = 0;
    char *buf = (char *)arena_alloc(dest, cap);
    for (;;) {
        if (len + 4096 > cap) {
            size_t new_cap = cap + 4096;
            char *grown = (char *)arena_alloc(dest, new_cap);
            memcpy(grown, buf, len);
            buf = grown;
            cap = new_cap;
        }
        ssize_t n = read(fd, buf + len, 4096);
        if (n <= 0) break;
        len += (size_t)n;
    }
    char *out = (char *)arena_alloc(dest, len + 1);
    memcpy(out, buf, len);
    out[len] = '\0';
    return out;
}

/* ---- buffered line reading (2026-08-25) ------------------------------
 * Real root cause found, not guessed: strace on the original byte-at-a-
 * time raw_at_eof_impl/raw_read_line_impl (turbogrep against 50 real
 * files) showed 2,699,542 real read() syscalls, 98.87% of total
 * runtime -- process startup (execve/mmap/mprotect) was a rounding
 * error (<0.001s combined). One read() syscall per BYTE is the actual
 * cost, not "the runtime is big" or any startup effect. Fix: a small,
 * real, per-fd buffer -- refilled via one real read() per IO_BUF_CAP
 * bytes instead of one per byte, cutting the syscall count by roughly
 * that factor. IO_MAX_HANDLES bounds concurrent buffered file handles
 * (real, honest, bounded, matching this whole codebase's own MAX_*
 * table conventions elsewhere) -- more than that degrades to the
 * original unbuffered behavior rather than failing outright, a real
 * but rare case (this stdlib's own real consumers, e.g. grep.prn, only
 * ever hold one file open at a time).
 *
 * Real, honest, narrow limitation NOT solved here: this buffer is only
 * ever filled/drained by raw_at_eof_impl/raw_read_line_impl -- mixing
 * read-line and read-string (raw_read_all_impl, which reads the real
 * fd directly, bypassing this buffer entirely) against the SAME open
 * FileHandle would silently drop or duplicate whatever's sitting in
 * the buffer. Not a problem for any real caller today (grep.prn only
 * ever uses read-line), flagged rather than solved with a bigger
 * unified-buffering rewrite this fix doesn't need yet. */
#define IO_BUF_CAP 4096
#define IO_MAX_HANDLES 32

typedef struct {
    int used;
    int fd;
    unsigned char buf[IO_BUF_CAP];
    size_t pos;
    size_t len;
} IoBufState;

static IoBufState g_io_bufs[IO_MAX_HANDLES]; /* zero-initialized (BSS): every
                                                 `used` starts false, real and
                                                 correct with no explicit
                                                 init code needed. */

static IoBufState *io_buf_for(int fd) {
    for (int i = 0; i < IO_MAX_HANDLES; i++) {
        if (g_io_bufs[i].used && g_io_bufs[i].fd == fd) return &g_io_bufs[i];
    }
    for (int i = 0; i < IO_MAX_HANDLES; i++) {
        if (!g_io_bufs[i].used) {
            g_io_bufs[i].used = 1;
            g_io_bufs[i].fd = fd;
            g_io_bufs[i].pos = 0;
            g_io_bufs[i].len = 0;
            return &g_io_bufs[i];
        }
    }
    return NULL; /* real, rare fallback -- see header comment above */
}

/* io_buf_release -- called from raw_close_impl. Real, load-bearing
 * correctness fix, not just cleanup: the OS is free to reuse a closed
 * fd's own integer value for the very next open() call in the same
 * process (turbogrep's own real usage pattern -- open/read/close one
 * file, then the next, in a loop) -- leaving a stale buffer keyed by
 * that fd number around would silently serve a NEW file's read-line
 * calls from the OLD file's leftover buffered bytes. */
static void io_buf_release(int fd) {
    for (int i = 0; i < IO_MAX_HANDLES; i++) {
        if (g_io_bufs[i].used && g_io_bufs[i].fd == fd) {
            g_io_bufs[i].used = 0;
            return;
        }
    }
}

static inline int raw_close_impl(int fd) {
    io_buf_release(fd);
    return close(fd) == 0 ? 0 : -1;
}

/* raw_at_eof_impl -- checks (and, if needed, refills) this fd's own
 * buffer; real EOF only once a real read() returns 0. Real, honest,
 * narrow scope carried over from the original unbuffered version:
 * correct for seekable regular files (real grep/sed/awk targets), not
 * pipes/sockets/terminals -- those never worked with the original
 * lseek-based peek either, not a regression. */
static inline int raw_at_eof_impl(int fd) {
    IoBufState *b = io_buf_for(fd);
    if (!b) {
        /* IO_MAX_HANDLES exceeded -- real, rare, honest fallback to the
         * original unbuffered peek rather than failing outright. */
        char c;
        ssize_t n = read(fd, &c, 1);
        if (n <= 0) return 1;
        lseek(fd, -1, SEEK_CUR);
        return 0;
    }
    if (b->pos < b->len) return 0;
    ssize_t n = read(fd, b->buf, IO_BUF_CAP);
    if (n <= 0) return 1;
    b->pos = 0;
    b->len = (size_t)n;
    return 0;
}

/* raw_read_line_impl -- reads from the already-primed buffer
 * (raw_at_eof_impl always runs first per io.prn's own read-line, so a
 * refill has already happened if one was needed), refilling again
 * mid-line via one more real read() only when the buffer runs dry
 * before the line does. Same real trailing-'\n'-consumed-not-included
 * convention as the original (Go's bufio.Scanner, Python's
 * str.splitlines default); a final line with no trailing newline
 * still returns everything read so far, same as a text editor
 * treating a missing trailing newline as still a real last line. */
static inline char *raw_read_line_impl(int fd, Arena *dest) {
    IoBufState *b = io_buf_for(fd);
    size_t cap = 128;
    size_t len = 0;
    char *out = (char *)arena_alloc(dest, cap);
    for (;;) {
        char c;
        if (b) {
            if (b->pos >= b->len) {
                ssize_t n = read(fd, b->buf, IO_BUF_CAP);
                if (n <= 0) break;
                b->pos = 0;
                b->len = (size_t)n;
            }
            c = (char)b->buf[b->pos++];
        } else {
            ssize_t n = read(fd, &c, 1);
            if (n <= 0) break;
        }
        if (c == '\n') break;
        if (len + 1 > cap) {
            size_t new_cap = cap * 2;
            char *grown = (char *)arena_alloc(dest, new_cap);
            memcpy(grown, out, len);
            out = grown;
            cap = new_cap;
        }
        out[len++] = c;
    }
    char *result = (char *)arena_alloc(dest, len + 1);
    memcpy(result, out, len);
    result[len] = '\0';
    return result;
}

/* raw_read_f64_impl -- gpt2.c's own real weight-file shape: 4-byte
 * float32 on disk, widened to PARENA's own only real float type (F64/
 * double) on read, matching io.prn's own read-floats real intent
 * (loaded weights are immediately reshaped/matmul'd as F64 downstream,
 * same as gpt2.c's own fread_or_fail). Short-read/EOF/error all
 * silently return 0.0 -- read-floats' own caller is expected to pass a
 * real, correct `n`, matching this whole file's "narrow, not
 * defensive beyond what's needed" scope. */
static inline double raw_read_f64_impl(int fd) {
    float f = 0.0f;
    ssize_t n = read(fd, &f, sizeof(float));
    (void)n;
    return (double)f;
}

#endif /* PARENA_RUNTIME_H */
