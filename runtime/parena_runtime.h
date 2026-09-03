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

/* _POSIX_C_SOURCE must be defined before any system header is
 * included: under -std=c99 (this project's own strict ISO build
 * flag, not -std=gnu99), glibc hides getaddrinfo/struct addrinfo and
 * the rest of the POSIX-only surface behind this feature-test macro.
 * Found for real (not assumed) getting stdlib/net/tcp.prn's new
 * tcp_connect_impl to actually gcc-compile -- a plain #include
 * <netdb.h> alone was not enough. 200112L = POSIX.1-2001, the version
 * that defines getaddrinfo. */
#define _POSIX_C_SOURCE 200112L
/* _DEFAULT_SOURCE alongside _POSIX_C_SOURCE (both may coexist under glibc,
 * unlike _POSIX_C_SOURCE alone) -- needed for pty_open_impl below:
 * forkpty/openpty are a real glibc/BSD extension declared in <pty.h>, not
 * POSIX-standard, and glibc hides them when _POSIX_C_SOURCE is defined
 * without this. Found for real getting stdlib/pty.prn's pty_open_impl to
 * actually see forkpty's declaration, the same "define the feature-test
 * macro before any system header, verify by actually compiling" discipline
 * tcp_connect_impl's own header comment above already documents. */
#define _DEFAULT_SOURCE

#include <stddef.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <signal.h>
/* Real cross-platform split (2026-08-26, founder: "ensure editor
 * binaries for windows linux and mac are released"): everything below
 * this point -- BSD sockets (net/tcp.prn), POSIX ptys (pty.prn),
 * fork/exec (process.prn) -- is genuinely platform-specific and has no
 * real Windows equivalent in the same shape (WinSock2 and ConPTY are
 * different APIs entirely, not drop-in replacements); macOS/BSD's own
 * pty API lives in <util.h>, not Linux glibc's <pty.h>. Guarded so a
 * program that doesn't call into net/tcp, pty, or process (editor-demo
 * is the real, concrete case that needed this) can still build clean
 * on a real Windows runner -- programs that DO need those stay
 * Linux/macOS-only, an honest, pre-existing limitation (pty.prn's own
 * header comment already documented Windows ConPTY as real, separate,
 * unstarted work; this doesn't change that, just stops it from ALSO
 * blocking unrelated Windows builds). */
#ifndef _WIN32
#include <sys/wait.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <poll.h>
#include <strings.h>
#if defined(__linux__)
#include <pty.h>
#elif defined(__APPLE__)
#include <util.h>
#endif
#endif
/* SDL2 -- built-in, same tier as core (STDLIB.md's own "sdl2" section:
 * "SDL2 is built in... no (import sdl2) line needed"), so its header is
 * unconditionally available here rather than gated behind a feature
 * macro. Real, honest dependency this adds to every build of this
 * runtime, not silently glossed over: any environment building PARENA
 * programs needs libsdl2-dev installed (confirmed present on this box).
 * Harmless for a program that never calls into stdlib/sdl2.prn -- every
 * sdl2_*_impl function below is `static inline`, so an unreferenced one
 * emits no symbol at all, no -lSDL2 needed unless something actually
 * calls one.
 *
 * Real, honest exception carved out here (2026-09-03, kanban priority-queue
 * card PARENA-0001, "smallest vertical slice to make PARENA work with the
 * OS... alpine? debian?"): a headless/embedded/musl-static build target
 * (real, live-checked motivating case -- Alpine's own real SDL2 dev headers
 * are built against musl, not this box's glibc-linked libsdl2-dev, so
 * mixing them fails; a real target with zero SDL2 in its own `.prn` source
 * at all, like `sip/message.prn` or `pentest/pcap.prn`, shouldn't need SDL2
 * headers present just to compile) can define `PARENA_NO_GRAPHICS` before
 * including this header to skip SDL2/SDL2_ttf entirely. Default (undefined)
 * behavior is unchanged -- every existing consumer still gets SDL2
 * unconditionally, exactly as before. */
#ifndef PARENA_NO_GRAPHICS
#include <SDL2/SDL.h>
/* SDL2_ttf -- real text rendering, the concrete next real extension
 * flagged when sdl2.prn's own renderer/draw calls were closed (2026-08-26):
 * "the next real extension once an editor loop actually needs to render
 * text." Same "unconditionally available, harmless if unused" reasoning
 * as SDL2 itself above -- confirmed libsdl2-ttf-dev present on this box. */
#include <SDL2/SDL_ttf.h>
#endif /* PARENA_NO_GRAPHICS */

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

/* string_contains_ci_impl -- real, portable case-insensitive substring
 * search for stdlib/string.prn's own contains-ci?. Originally called
 * glibc's own strcasestr directly inline -- real, genuinely portable
 * everywhere this runtime had been verified before (Linux) -- but a
 * glibc/BSD extension, not standard, and not what MinGW/Windows
 * provides (found for real cross-compiling editor-demo under
 * x86_64-w64-mingw32-gcc, 2026-08-26: a real "implicit declaration of
 * function 'strcasestr'" warning/link risk, not assumed). A real,
 * hand-written O(n*m) scan instead -- correct on every real platform
 * this runtime targets, and haystack/needle here are real editor-sized
 * strings (grammar keywords, file paths), not a hot path that needs a
 * faster real algorithm. */
static inline int string_contains_ci_impl(const char *haystack, const char *needle) {
    size_t hlen = strlen(haystack);
    size_t nlen = strlen(needle);
    if (nlen == 0) return 1;
    if (nlen > hlen) return 0;
    for (size_t i = 0; i + nlen <= hlen; i++) {
        size_t j = 0;
        while (j < nlen && tolower((unsigned char)haystack[i + j]) == tolower((unsigned char)needle[j])) {
            j++;
        }
        if (j == nlen) return 1;
    }
    return 0;
}

/* ---- stdlib/io.prn real host glue (2026-08-24) ----------------------
 * Raw POSIX fd-based primitives only -- every Result/Option/FileHandle
 * value io.prn itself constructs is built with ordinary PARENA syntax
 * (Ok/Err/{:field val}), not here. Real, honest, narrow scope: each
 * function below returns a plain scalar or string, no boxing -- see
 * io.prn's own header comment for the full reasoning on why the split
 * is drawn exactly here. */
/* O_BINARY (2026-08-27, real, confirmed-live bug: founder opened a
 * real, plain CRLF .bat file in the editor and got the "looks like a
 * binary file" placeholder). O_BINARY only exists on Windows -- on
 * POSIX there's no text/binary distinction and open() never needs it,
 * so this is a real no-op #define there, not a behavior change.
 * Without it, MinGW's own CRT opens every file in TEXT mode by
 * default: CRLF is silently collapsed to LF on read (and LF silently
 * expanded back to CRLF on write), and a raw 0x1A byte is treated as
 * an early EOF marker. Every real Windows-authored text file this
 * session has actually looked at (build_win.bat, PLAY.bat, ...) is
 * CRLF-terminated -- read in text mode, the returned String is
 * shorter than the real on-disk stat() size by exactly its own
 * newline count, which is precisely the mismatch looks_like_binary
 * checks for. Same real bug class on the WRITE side too: saving would
 * have silently rewritten a real file's own line endings. Applied
 * unconditionally (read/write/append all get it) so this editor
 * always treats a file's bytes as its own, not the CRT's translated
 * view of them -- matches this whole session's own "byte-exact,
 * stat-size-vs-strlen must actually mean something" discipline. */
#ifndef O_BINARY
#define O_BINARY 0
#endif

static inline int raw_open_impl(const char *path, int mode_tag) {
    int flags;
    switch (mode_tag) {
        case 0: flags = O_RDONLY | O_BINARY; break;                      /* Read */
        case 1: flags = O_WRONLY | O_CREAT | O_TRUNC | O_BINARY; break;  /* Write */
        case 2: flags = O_WRONLY | O_CREAT | O_APPEND | O_BINARY; break; /* Append */
        default: flags = O_RDONLY | O_BINARY; break;
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
 *
 * Real, confirmed-live bug fixed here (2026-08-27, founder actually
 * dropping a real large file onto the PARENA editor: "it crashed...
 * if we try to open a too large file it just chokes"): this used to
 * grow in FIXED 4096-byte chunks, reallocating (not resizing in
 * place -- this is a bump arena, the old buffer is never freed) and
 * copying the ENTIRE buffer so far on every single grow step. For an
 * N-byte file that's N/4096 grow steps, each copying up to N bytes --
 * real O(N^2) copy work, and since every intermediate buffer stays
 * permanently allocated (arena, not a real realloc), real O(N^2)
 * WASTED memory too: a 100MB file needs roughly 1.2TB of total arena
 * allocations across every intermediate step, not the 100MB anyone
 * would expect -- the real, confirmed root cause of "chokes"/crashes
 * on a large file, not a vague performance concern.
 *
 * Fixed with the real, obvious right answer for a REGULAR FILE
 * specifically (raw-read-all's only real caller, io.prn's own
 * read-string, is documented as FileHandle-only): fstat(2) the real
 * file size up front and allocate that in ONE shot -- zero grow
 * steps, zero wasted intermediate buffers, for the real common case.
 * fstat can still be wrong (the file grows while being read, or a
 * non-regular fd somehow reaches here) -- real doubling growth (not
 * the old fixed +4096) is kept as the real fallback for exactly that
 * case, so this stays correct even when the size hint is, not just
 * fast when it's right.
 *
 * Real, confirmed-live SECOND bug found and fixed here (2026-08-28,
 * founder real-time: "when i open a large file its dog shit slow we
 * dont want to load the whole thing into memory"): the fast path
 * above (fstat succeeded) was STILL silently paying for a full extra
 * copy of the whole file. Root cause, confirmed via direct
 * instrumentation against a real 119MB test file (cap ended up
 * EXACTLY 2x the real file size -- one full, needless doubling, not
 * the "zero grow steps" the fstat fast path was supposed to
 * guarantee): a real EOF can only ever be detected by actually
 * calling read() with a NONZERO size and getting 0 back -- there is
 * no way to know "the file has no more bytes" without asking. A cap
 * sized to EXACTLY st_size+1 leaves no room left for that one
 * confirming call once every real content byte has been read, so the
 * old code (any version that tries to guarantee room for a real
 * read() request right up to the exact byte) is forced to grow the
 * WHOLE buffer just to make room for one call that's going to return
 * 0 bytes anyway.
 *
 * Real fix: size `cap` with `st_size + 4097` instead of `st_size + 1`
 * -- the real content, PLUS one full extra 4096-byte read-chunk's
 * worth of headroom (so the loop can always make one more real,
 * nonzero-sized read attempt right at the true end and get back a
 * real EOF signal without ever needing to grow first), PLUS 1 for the
 * NUL terminator. 4096 bytes of slack is negligible next to any file
 * large enough for this to matter (0.003% of the 119MB test file),
 * and the growth check itself (`len + 4096 >= cap`) can never fire
 * while reading real content anymore, since cap is always at least
 * 4096 bytes ahead of the real file size. A real `grew` flag (not an
 * exact `cap == len + 1` size comparison, which the 4097-byte slack
 * would always fail even in the genuinely no-growth case) tracks
 * whether a real overshoot doubling ever actually happened -- only
 * then is the trim-copy below real, needed work; the ordinary case
 * (fstat right, file didn't grow mid-read) skips it entirely. */
static inline char *raw_read_all_impl(int fd, Arena *dest) {
    size_t cap = 4096;
    int have_size_hint = 0;
    struct stat st;
    if (fstat(fd, &st) == 0 && st.st_size > 0) {
        cap = (size_t)st.st_size + 4097;
        have_size_hint = 1;
    }
    size_t len = 0;
    char *buf = (char *)arena_alloc(dest, cap);
    int grew = 0;
    for (;;) {
        if (len + 4096 >= cap) {
            size_t new_cap = cap * 2;
            char *grown = (char *)arena_alloc(dest, new_cap);
            memcpy(grown, buf, len);
            buf = grown;
            cap = new_cap;
            grew = 1;
        }
        size_t want = cap - len - 1; /* leave room for the trailing NUL */
        if (want > 4096) want = 4096;
        ssize_t n = read(fd, buf + len, want);
        if (n <= 0) break;
        len += (size_t)n;
    }
    if (!grew && have_size_hint) {
        buf[len] = '\0';
        return buf;
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

/* Real BSD sockets (net/tcp.prn) and real POSIX ptys (pty.prn) below --
 * both genuinely unavailable on Windows (see this file's own top-of-
 * file header comment on the include split for the full reasoning),
 * guarded so a program that doesn't need either (editor-demo) can
 * still build clean there. */
#ifndef _WIN32

/* ---- stdlib/net/tcp.prn real host glue (2026-08-25) ------------------
 * Real BSD sockets -- net/tcp.prn's own #target bodies previously
 * declared `tcp_listen`/`tcp_accept`/`tcp_connect`/`tcp_read`/
 * `tcp_write` but this runtime never actually implemented any of them
 * ("FFI declared, host implementation not written yet", the exact same
 * class of gap io.prn's own 2026-08-24 rewrite closed for file I/O —
 * see that section's header comment above for the full reasoning this
 * mirrors). Closed here for real, first real network I/O anywhere in
 * this language: net/tcp.prn itself was also carrying io.prn's
 * pre-fix bug (a `#target` body declared to return `Result`/`TcpStream`
 * directly, which VS0 never auto-boxes -- see emit.c's own
 * find_target_c_src comment) until this same pass fixed it to match
 * io.prn's now-established shape: raw primitives return a plain
 * scalar/string here, Result/Option/struct construction happens in
 * ordinary PARENA source in net/tcp.prn itself.
 *
 * Real, honest, narrow limitation, stated plainly rather than silently
 * assumed away: tcp_read_impl below reads until the peer closes the
 * connection or a read() error, the same one-shot shape
 * raw_read_all_impl above already uses for files. That is CORRECT for
 * a server that closes after responding (HTTP/1.0-style, or HTTP/1.1
 * with a `Connection: close` request header -- which is exactly why
 * net/http.prn's own request builder sends one) and WRONG for a
 * keep-alive connection the peer intends to reuse -- reading would
 * simply block forever waiting for an EOF that never comes. No
 * Content-Length-aware early stop or chunked-encoding support exists
 * yet; a real future gap, not this pass's scope. */
static inline int tcp_connect_impl(const char *host, int port) {
    struct addrinfo hints, *res, *rp;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    char portstr[16];
    snprintf(portstr, sizeof portstr, "%d", port);
    if (getaddrinfo(host, portstr, &hints, &res) != 0) return -1;
    int fd = -1;
    for (rp = res; rp != NULL; rp = rp->ai_next) {
        fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd < 0) continue;
        if (connect(fd, rp->ai_addr, rp->ai_addrlen) == 0) break;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    return fd;
}

static inline int tcp_listen_impl(int port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof opt);
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons((uint16_t)port);
    if (bind(fd, (struct sockaddr *)&addr, sizeof addr) < 0) { close(fd); return -1; }
    if (listen(fd, 16) < 0) { close(fd); return -1; }
    return fd;
}

static inline int tcp_accept_impl(int listener_fd) {
    return accept(listener_fd, NULL, NULL);
}

/* find_body_length_from_headers -- scans `buf[0..have)` for the real
 * header/body boundary ("\r\n\r\n") and, once found, a real
 * case-insensitive "Content-Length:" header within the header block.
 * Returns -1 if the boundary hasn't arrived yet (caller should keep
 * reading), -2 if the boundary arrived but no real Content-Length header
 * was found (caller should fall back to read-until-EOF/timeout), or the
 * real, parsed Content-Length value (>= 0) with *header_end_out set to the
 * byte offset where the real body starts. */
static inline long find_body_length_from_headers(const char *buf, size_t have, size_t *header_end_out) {
    const char *boundary = NULL;
    for (size_t i = 0; i + 3 < have; i++) {
        if (buf[i] == '\r' && buf[i + 1] == '\n' && buf[i + 2] == '\r' && buf[i + 3] == '\n') {
            boundary = buf + i;
            break;
        }
    }
    if (!boundary) return -1;
    size_t header_len = (size_t)(boundary - buf);
    *header_end_out = header_len + 4;
    for (size_t i = 0; i + 15 <= header_len; i++) {
        if (strncasecmp(buf + i, "Content-Length:", 15) == 0) {
            long v = strtol(buf + i + 15, NULL, 10);
            return v >= 0 ? v : -2;
        }
    }
    return -2;
}

/* tcp_read_impl -- same grow-by-4096-and-copy shape as
 * raw_read_all_impl above, deliberately not shared code: that one is
 * documented as the io.prn-specific real host glue, this one is
 * net/tcp.prn's own, and the two stdlib files are never combined in
 * the same real build (net/tcp.prn's own header comment already
 * documents an identical reason for NetError's own duplication) so
 * keeping them textually separate costs nothing and avoids coupling
 * either file's future changes to the other's.
 *
 * Content-Length-aware since 2026-09-02 (see PARENA's own CHANGELOG for the
 * real trace): a real WebDriver server (chromedriver) sends a real
 * Content-Length header but does NOT actually close the TCP connection
 * after responding, despite its own response advertising
 * "Connection:close" -- confirmed live two ways: a byte-identical request
 * sent over a plain shell /dev/tcp socket also blocks forever waiting for
 * an EOF that never comes, and a raw C reproduction calling this exact
 * function directly (bypassing every other layer) hung the same way.
 * Reading strictly until EOF -- this function's entire previous behavior
 * -- therefore hung forever on every single real request to a real
 * WebDriver server, not a webdriver.prn-specific bug. Fixed generically
 * here (every real net/tcp.prn caller benefits, not just http.prn): once
 * the header/body boundary is seen and a real Content-Length value is
 * found, stop reading the moment that many body bytes have arrived,
 * without waiting for the peer to close. Falls back to the original
 * read-until-EOF behavior when no Content-Length header is present (a
 * real, honestly-scoped limitation for chunked/close-delimited responses,
 * unchanged from before). A bounded 30s idle-read timeout (via poll) is a
 * real, cheap safety net against ever hanging forever again on a response
 * shape this function's own parsing can't make sense of. */
static inline char *tcp_read_impl(int fd, Arena *dest) {
    size_t cap = 4096;
    size_t len = 0;
    char *buf = (char *)arena_alloc(dest, cap);
    long content_length = -1; /* -1 = not yet known (boundary not seen, or no Content-Length header) */
    size_t header_end = 0;
    for (;;) {
        if (content_length >= 0 && len >= header_end + (size_t)content_length) break;
        if (len + 4096 > cap) {
            size_t new_cap = cap + 4096;
            char *grown = (char *)arena_alloc(dest, new_cap);
            memcpy(grown, buf, len);
            buf = grown;
            cap = new_cap;
        }
        struct pollfd pfd;
        pfd.fd = fd;
        pfd.events = POLLIN;
        int pr = poll(&pfd, 1, 30000);
        if (pr <= 0) break; /* timeout or poll error -- return whatever's been read so far */
        ssize_t n = read(fd, buf + len, 4096);
        if (n <= 0) break;
        len += (size_t)n;
        if (content_length < 0) {
            long cl = find_body_length_from_headers(buf, len, &header_end);
            if (cl >= 0) content_length = cl;
            /* cl == -2 (boundary seen, no real Content-Length): keep the
               original read-until-EOF/timeout behavior. cl == -1
               (boundary not seen yet): keep reading. */
        }
    }
    char *out = (char *)arena_alloc(dest, len + 1);
    memcpy(out, buf, len);
    out[len] = '\0';
    return out;
}

/* tcp_write_impl -- byte-for-byte the same loop as string_concat's
 * sibling raw_write_impl above (write() is write() whether the fd is a
 * file or a socket); kept as its own real, distinctly-named function
 * rather than reused across the file/socket boundary, matching this
 * runtime's own established preference for names that say what they
 * are (net/tcp.prn's own header comment on NetError draws the same
 * file/socket line for the identical reason). */
static inline int tcp_write_impl(int fd, const char *s) {
    size_t len = strlen(s);
    size_t written = 0;
    while (written < len) {
        ssize_t n = write(fd, s + written, len - written);
        if (n < 0) return -1;
        written += (size_t)n;
    }
    return 0;
}

static inline int tcp_close_impl(int fd) {
    return close(fd) == 0 ? 0 : -1;
}

#endif /* !_WIN32 -- end of net/tcp.prn real host glue. Nothing
        * cross-platform currently includes net/tcp.prn (editor-demo's
        * own file list never has), so it stays exactly as it was --
        * genuinely absent on Windows, not stubbed. */

/* pty.prn gets its OWN guard, separate from net/tcp.prn's above
 * (2026-08-27, real CI break found live: the editor's new terminal-
 * toggle feature added stdlib/pty.prn + stdlib/shell.prn to
 * editor-demo's own file list, which DOES build cross-platform --
 * Windows Build Editor job failed with "implicit declaration of
 * function 'pty_open_impl'" etc. once pty.prn's functions were
 * expected to exist there too, since the old shared #ifndef _WIN32
 * block simply didn't define them on Windows at all). Real Windows
 * ConPTY support is still genuinely unstarted (pty.prn's own header
 * comment already says so) -- the #else stubs below let the terminal-
 * toggle FEATURE compile and link cleanly cross-platform and fail
 * HONESTLY at runtime (a real, visible SpawnFailed the editor already
 * shows as "(terminal not spawned...)", not a silent lie or a crash)
 * rather than breaking the whole cross-platform editor build over one
 * genuinely POSIX-only backend. */
#ifndef _WIN32

/* ---- stdlib/pty.prn real host glue (2026-08-26) ------------------------
 * The concrete "PARENA eats PITVIPER" dogfooding step NORTHSTAR.md's own
 * strangler-fig section names -- a real, direct generalization of
 * PITVIPER's own shipped internal/pty/pty_linux.go (openpty(3)-based; the
 * Windows ConPTY half PITVIPER also has is real, separate, unstarted host
 * glue here, same honest boundary this file already draws elsewhere for
 * anything not reachable from this box). Closes the same real gap class
 * tcp_*_impl above already closed for net/tcp.prn: pty.prn's own
 * previous version declared its #target bodies to return Result/Pty
 * directly, which VS0 never auto-boxes -- these are the raw
 * scalar-returning primitives pty.prn's rewrite calls instead, Result/
 * struct construction happens in ordinary PARENA source there.
 *
 * Real, honest, narrow limitation, stated plainly: no pid is tracked or
 * returned -- same deliberate scope process.prn's own real host glue
 * already carries ("fork+exec, detached -- no pipe/wait plumbing"). A
 * caller can read/write/resize/close the pty by fd, but can't waitpid or
 * signal the child shell directly; a real future gap if PITVIPER's own
 * port ever needs to detect "the shell process itself exited" instead of
 * inferring it from read() returning EOF. */
static inline int pty_open_impl(const char *shell, int cols, int rows) {
    struct winsize ws;
    memset(&ws, 0, sizeof ws);
    ws.ws_col = (unsigned short)cols;
    ws.ws_row = (unsigned short)rows;
    int master_fd;
    pid_t pid = forkpty(&master_fd, NULL, NULL, &ws);
    if (pid < 0) return -1;
    if (pid == 0) {
        setenv("TERM", "xterm-256color", 1);
        execlp(shell, shell, (char *)NULL);
        _exit(127);
    }
    return master_fd;
}

/* Same grow-by-4096-and-copy shape as tcp_read_impl above, kept as its
 * own distinctly-named function rather than shared -- same reasoning
 * tcp_read_impl's own header comment already gives for not sharing with
 * raw_read_all_impl (io.prn), applied one boundary further out. */
static inline char *pty_read_impl(int fd, Arena *dest) {
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

/* pty_poll_read_impl -- real, new, NON-blocking sibling to
 * pty_read_impl above (2026-08-27, real gap found integrating pty.prn
 * into the PARENA editor's own terminal panel, founder real-time:
 * "toggle between terminal and editor... work just like pitviper").
 * pty_read_impl's own real, documented contract ("reads until the
 * peer side closes") is exactly right for a "run one command, wait
 * for it to finish" caller (test_shell.c's own real usage) but wrong
 * for an interactive, long-lived shell a UI render loop polls every
 * frame -- calling a blocking read() against an idle shell sitting at
 * its next prompt would freeze the WHOLE editor, not just the
 * terminal, until the user's next keystroke produced output.
 *
 * Deliberately does NOT set O_NONBLOCK on the fd itself (which would
 * have silently changed pty_read_impl's own existing, tested
 * blocking behavior for every caller, including test_shell.c's own
 * real "write a command, read until EOF" check -- a real regression
 * risk, not a safe change to make to a shared fd's own mode). Instead
 * uses a real, standard poll(2) with a ZERO timeout to check "is
 * there data ready right now" before ever calling read() at all --
 * read() itself is only reached when poll confirms data (or EOF) is
 * actually pending, so it can never block. Returns a real, valid
 * (often empty) String either way -- "" is the correct, honest
 * "nothing new this frame" signal, not an error. */
static inline char *pty_poll_read_impl(int fd, Arena *dest) {
    struct pollfd pfd;
    pfd.fd = fd;
    pfd.events = POLLIN;
    pfd.revents = 0;
    int pr = poll(&pfd, 1, 0);
    if (pr <= 0 || !(pfd.revents & (POLLIN | POLLHUP | POLLERR))) {
        char *out = (char *)arena_alloc(dest, 1);
        out[0] = '\0';
        return out;
    }
    char buf[4096];
    ssize_t n = read(fd, buf, sizeof buf);
    if (n <= 0) {
        char *out = (char *)arena_alloc(dest, 1);
        out[0] = '\0';
        return out;
    }
    char *out = (char *)arena_alloc(dest, (size_t)n + 1);
    memcpy(out, buf, (size_t)n);
    out[n] = '\0';
    return out;
}

static inline int pty_write_impl(int fd, const char *s) {
    size_t len = strlen(s);
    size_t written = 0;
    while (written < len) {
        ssize_t n = write(fd, s + written, len - written);
        if (n < 0) return -1;
        written += (size_t)n;
    }
    return 0;
}

static inline int pty_resize_impl(int fd, int cols, int rows) {
    struct winsize ws;
    memset(&ws, 0, sizeof ws);
    ws.ws_col = (unsigned short)cols;
    ws.ws_row = (unsigned short)rows;
    return ioctl(fd, TIOCSWINSZ, &ws) == 0 ? 0 : -1;
}

static inline int pty_close_impl(int fd) {
    return close(fd) == 0 ? 0 : -1;
}

#else /* _WIN32 -- real Windows ConPTY backend genuinely not written
       * yet (see this block's own opening comment above). These
       * stubs exist purely so a cross-platform target that includes
       * pty.prn compiles and links on Windows; every real caller goes
       * through pty-open first (stdlib/pty.prn), which turns this -1
       * into a real Err(SpawnFailed) the editor's own terminal-toggle
       * feature already handles and reports honestly -- these never
       * silently claim success. */
static inline int pty_open_impl(const char *shell, int cols, int rows) {
    (void)shell; (void)cols; (void)rows;
    return -1;
}

static inline char *pty_read_impl(int fd, Arena *dest) {
    (void)fd;
    char *out = (char *)arena_alloc(dest, 1);
    out[0] = '\0';
    return out;
}

static inline char *pty_poll_read_impl(int fd, Arena *dest) {
    (void)fd;
    char *out = (char *)arena_alloc(dest, 1);
    out[0] = '\0';
    return out;
}

static inline int pty_write_impl(int fd, const char *s) {
    (void)fd; (void)s;
    return -1;
}

static inline int pty_resize_impl(int fd, int cols, int rows) {
    (void)fd; (void)cols; (void)rows;
    return -1;
}

static inline int pty_close_impl(int fd) {
    (void)fd;
    return -1;
}
#endif /* _WIN32 -- end of pty.prn real host glue / Windows stub */

/* ---- stdlib/shell.prn real host glue (2026-08-26) ----------------------
 * A real, direct port of PITVIPER's own shell-resolution policy
 * (internal/pty/pty_windows.go's Open()/isWslStub/findGitBash). Every
 * function here is a genuinely irreducible raw OS primitive (env read,
 * PATH search, file existence) -- the real decision chain (explicit >
 * $SHELL > Git Bash off PATH > Git Bash well-known paths > platform
 * fallback) is real PARENA `match`/`cond` logic in shell.prn itself, not
 * hidden in here, matching that file's own stated design intent.
 *
 * Each function returns a plain empty string ("") as its "not found"
 * sentinel rather than NULL -- consistent with this runtime's own
 * established "raw primitives return a plain scalar/string, Option/
 * Result construction happens in ordinary PARENA source" convention
 * (tcp_read_impl et al. above), and avoids the caller ever having to
 * null-check a raw C pointer from PARENA source directly. */
static inline char *env_get_impl(const char *name, Arena *dest) {
    const char *v = getenv(name);
    if (v == NULL) v = "";
    size_t len = strlen(v);
    char *out = (char *)arena_alloc(dest, len + 1);
    memcpy(out, v, len + 1);
    return out;
}

/* exec_lookpath_impl -- real, portable PATH search, mirrors Go's own
 * exec.LookPath (checked for X_OK on POSIX; Windows PATHEXT resolution
 * is real, separate, unstarted host glue here, same honest "not
 * reachable from this box" boundary pty_open_impl's own header comment
 * already draws -- this box is Linux). */
static inline char *exec_lookpath_impl(const char *name, Arena *dest) {
    const char *path_env = getenv("PATH");
    if (path_env == NULL || path_env[0] == '\0') {
        char *out = (char *)arena_alloc(dest, 1);
        out[0] = '\0';
        return out;
    }
    size_t path_len = strlen(path_env);
    char *path_copy = (char *)malloc(path_len + 1);
    memcpy(path_copy, path_env, path_len + 1);
    char *saveptr = NULL;
    char *dir = strtok_r(path_copy, ":", &saveptr);
    char candidate[4096];
    while (dir != NULL) {
        snprintf(candidate, sizeof candidate, "%s/%s", dir, name);
        if (access(candidate, X_OK) == 0) {
            size_t len = strlen(candidate);
            char *out = (char *)arena_alloc(dest, len + 1);
            memcpy(out, candidate, len + 1);
            free(path_copy);
            return out;
        }
        dir = strtok_r(NULL, ":", &saveptr);
    }
    free(path_copy);
    char *out = (char *)arena_alloc(dest, 1);
    out[0] = '\0';
    return out;
}

static inline int file_exists_impl(const char *path) {
    return access(path, F_OK) == 0 ? 1 : 0;
}

/* list_dir_impl -- real, portable directory listing (2026-08-27,
 * founder real-time: "we are going to need to do the nerd tree style
 * implementation to display a tree of files"). Returns a real,
 * newline-joined list of entry names in `path` (both files and
 * subdirectories, "." and ".." skipped), or an empty string on any
 * error (path doesn't exist, not a directory, permission denied) --
 * same "empty string on failure, not NULL" convention env_get_impl/
 * exec_lookpath_impl above already establish; stdlib/io.prn's own
 * list-dir wrapper splits this on "\n" via the already-real
 * string/split, special-casing "" to a real empty Vec rather than a
 * Vec holding one blank entry (string/split's own real, documented
 * behavior on an empty input string). dirent.h's opendir/readdir/
 * closedir are genuinely portable here -- MinGW-w64 ships a real
 * dirent.h emulation (backed by FindFirstFile/FindNextFile
 * internally), confirmed via a real local mingw cross-compile, so
 * this needs no #ifdef _WIN32 split unlike net/tcp.prn's sockets or
 * pty.prn's forkpty elsewhere in this file. Real, honest v0: one flat
 * level only, not recursive -- a real tree view expanding a
 * subdirectory is separate, deferred UI work the file-tree sidebar
 * built on top of this doesn't have yet either. */
static inline char *list_dir_impl(const char *path, Arena *dest) {
    DIR *d = opendir(path);
    if (d == NULL) {
        char *out = (char *)arena_alloc(dest, 1);
        out[0] = '\0';
        return out;
    }
    size_t cap = 4096;
    size_t len = 0;
    char *buf = (char *)arena_alloc(dest, cap);
    struct dirent *ent;
    int first = 1;
    while ((ent = readdir(d)) != NULL) {
        const char *name = ent->d_name;
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;
        size_t nlen = strlen(name);
        size_t need = nlen + (first ? 0 : 1);
        if (len + need + 1 > cap) {
            size_t new_cap = cap;
            while (len + need + 1 > new_cap) new_cap += 4096;
            char *grown = (char *)arena_alloc(dest, new_cap);
            memcpy(grown, buf, len);
            buf = grown;
            cap = new_cap;
        }
        if (!first) buf[len++] = '\n';
        memcpy(buf + len, name, nlen);
        len += nlen;
        first = 0;
    }
    closedir(d);
    buf[len] = '\0';
    return buf;
}

/* is_dir_impl -- real, portable directory test (2026-08-27, closing
 * the file-tree sidebar's own honest v0.83.0 gap: "clicking a
 * subdirectory entry currently opens it as if it were a file").
 * Deliberately reuses opendir()'s own real success/failure rather than
 * a separate stat()/GetFileAttributes call -- one real portable
 * primitive already proven to work cross-platform (list_dir_impl
 * above), not a second one; real, honest false on anything that isn't
 * a real, openable directory (nonexistent path, a regular file, a
 * permission-denied directory all alike -- this repo's own
 * path-exists? already covers plain existence). */
static inline int is_dir_impl(const char *path) {
    DIR *d = opendir(path);
    if (d == NULL) return 0;
    closedir(d);
    return 1;
}

/* ---- stdlib/process.prn real host glue (2026-08-25) -------------------
 * Real fork+exec, detached (no pipe/wait plumbing -- this stdlib's own
 * real, narrow scope is "start an external helper process and later
 * kill it by pid", the exact shape a test harness spawning a fixture
 * server needs, not a general subprocess/IPC library). A double-fork
 * is deliberately NOT used: the child stays this process's direct
 * child so process_kill_impl's real pid is meaningful and a stray
 * child is visible to `ps` rooted at this process, matching the "start
 * it, kill it yourself" contract callers get -- no orphan-and-forget
 * daemonization semantics assumed.
 *
 * Guarded on Windows (2026-08-26, same reasoning as the net/tcp.prn +
 * pty.prn block above): fork()/execl()/pid_t/kill() are genuinely
 * POSIX-only, no drop-in Windows equivalent (CreateProcess/
 * TerminateProcess are a different shape entirely) -- a real, separate,
 * unstarted port, not attempted here since editor-demo (the real
 * Windows build this guard exists for) doesn't need process.prn at
 * all. */
#ifndef _WIN32
static int g_run_capture_last_exit_code = 0;

static inline int spawn_detached_impl(const char *path, const char *arg1) {
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        /* Child: replace this process image entirely. execl only
         * returns on failure. */
        execl(path, path, arg1, (char *)NULL);
        _exit(127);
    }
    return (int)pid;
}

static inline int process_kill_impl(int pid) {
    return kill((pid_t)pid, SIGTERM) == 0 ? 0 : -1;
}

/* run_capture_impl -- real, general "run a shell command, capture its stdout" primitive, added
 * 2026-09-02 filling in process.prn's own header comment's already-named "real, separate, future
 * scope: captured stdout/stderr" gap (LO/FRAMEWORK_NORTHSTAR.md's own event-sourcing extension:
 * SQL projectors shell out to real DB CLI clients -- sqlite3/mysql/psql -- rather than binding
 * each one's own native C client library via FFI). Real, deliberate choice: `popen`, not
 * `spawn_detached_impl`'s own fork+exec -- this needs to capture a pipe and wait synchronously
 * for exit, the opposite of spawn's own "detached, caller kills it later" contract; `popen` is
 * the real, standard, portable-enough (POSIX + Windows' own `_popen`, though this file's own
 * guard doesn't currently extend that far -- see below) libc primitive for exactly this.
 *
 * Real, honest, named limitations, not silently glossed over:
 * - `cmd` runs through `/bin/sh -c` -- any caller building `cmd` by concatenating untrusted
 *   data (a user-supplied field value with a shell metacharacter in it) has a real shell-
 *   injection risk. This file's own callers (SQL projectors) are responsible for either shell-
 *   escaping interpolated values or avoiding shell interpolation entirely (e.g. via each CLI
 *   tool's own `--defaults-extra-file`/parameterized-input mechanism) -- not solved generically
 *   here, named as a real, separate, security-relevant follow-up.
 * - stderr is NOT captured (shares the caller's own stderr) -- a real, separate follow-up if a
 *   caller ever needs it multiplexed or suppressed.
 * - The real exit code is stashed in a static (matching this file's own established "last
 *   operation status" side-channel convention, e.g. raw_errno_impl) rather than returned
 *   directly, since this function's own return type is the captured text -- read back via
 *   run_capture_last_exit_code_impl. Deliberately NOT re-run to learn the exit code separately:
 *   re-running a side-effecting command (a SQL write) to check its own status would be a real,
 *   silent double-execution bug, not just wasteful.
 * - Windows-guarded the same as this section's own existing spawn_detached_impl/
 *   process_kill_impl, even though `popen`/`_popen` themselves exist on Windows too -- because
 *   WIFEXITED/WEXITSTATUS (sys/wait.h) are POSIX-only; `_pclose`'s own real Windows return value
 *   is already the raw exit code with no macro unwrapping needed, a real, separate, un-started
 *   port if this framework ever needs to run on Windows. */
static inline char *run_capture_impl(const char *cmd, Arena *dest) {
    FILE *p = popen(cmd, "r");
    if (p == NULL) {
        g_run_capture_last_exit_code = -1;
        char *out = (char *)arena_alloc(dest, 1);
        out[0] = '\0';
        return out;
    }
    size_t cap = 4096;
    size_t len = 0;
    char *buf = (char *)arena_alloc(dest, cap);
    char chunk[4096];
    size_t n;
    while ((n = fread(chunk, 1, sizeof(chunk), p)) > 0) {
        if (len + n + 1 > cap) {
            size_t new_cap = cap * 2;
            while (len + n + 1 > new_cap) new_cap *= 2;
            char *new_buf = (char *)arena_alloc(dest, new_cap);
            memcpy(new_buf, buf, len);
            buf = new_buf;
            cap = new_cap;
        }
        memcpy(buf + len, chunk, n);
        len += n;
    }
    buf[len] = '\0';
    int status = pclose(p);
    g_run_capture_last_exit_code = (status >= 0 && WIFEXITED(status)) ? WEXITSTATUS(status) : -1;
    return buf;
}

static inline int run_capture_last_exit_code_impl(void) {
    return g_run_capture_last_exit_code;
}
#endif /* !_WIN32 -- end of process.prn real host glue */

/* ---- real `(current-arena)` builtin (2026-08-25) -----------------------
 * See src/emit.c's own `is_call_named(expr, "current-arena")` comment
 * for the full real reasoning. g_process_arena is zero-initialized by
 * plain C static-storage rules -- `{a->head = NULL}` is arena_init's
 * own entire body, so no explicit init call or constructor is needed
 * here at all. Never freed (a process-lifetime Arena, by design). */
static Arena g_process_arena;

static inline Arena *parena_current_arena(void) {
    return &g_process_arena;
}

#ifndef PARENA_NO_GRAPHICS
/* ---- stdlib/sdl2.prn real host glue (2026-08-26) ------------------------
 * The concrete "next real extension" STDLIB.md's own `sdl2` section
 * already flagged as pending ("full renderer/texture calls... left out
 * of this pass... the next real extension once a renderer-owning
 * program actually needs it") -- this is that program: the first real
 * slice of a PARENA-authored editor shell, PITVIPER's own real
 * SDL_CreateRenderer/SetDrawColor/Clear/FillRect/Copy/Present call shape
 * (cmd/pitviper/main.go's own renderFrame) ported one real primitive at
 * a time, cell-background rendering first (Copy/glyph-texture blitting
 * is real, separate, deferred follow-up work -- needs font/texture
 * loading this pass doesn't add).
 *
 * Window/Renderer are real opaque handle tables (a small fixed array of
 * real SDL_Window pointers and SDL_Renderer pointers, indexed by a plain
 * I32 handle), not a raw pointer carried through PARENA source directly
 * -- a deliberate,
 * pragmatic choice: this stdlib's own established struct convention
 * (Pty{fd:I32}, FileHandle{fd:I32}) already proves I32-handle structs
 * work end-to-end through VS0's real Result/Option boxing; a struct
 * field holding a raw C pointer is untested territory in this compiler
 * and not worth risking on this pass. */
#define SDL2_MAX_WINDOWS 8
#define SDL2_MAX_RENDERERS 8
static SDL_Window *g_sdl2_windows[SDL2_MAX_WINDOWS];
static int g_sdl2_window_count = 0;
static SDL_Renderer *g_sdl2_renderers[SDL2_MAX_RENDERERS];
static int g_sdl2_renderer_count = 0;

static inline int sdl2_init_impl(void) {
    /* Real, confirmed-live bug fix (2026-08-27, founder real-time,
     * actively testing on real Windows: "drag to select doesnt work" +
     * "click to insert cursor doesnt work" -- BOTH core mouse features
     * broken together, not two separate bugs, pointed straight at a
     * real, systemic mouse-COORDINATE problem). SDL2's own real,
     * documented default on Windows is DPI-UNAWARE (SDL_HINT_WINDOWS_
     * DPI_AWARENESS defaults to "" -- "do not change the DPI
     * awareness"): on any real display with non-100% scaling (very
     * common on real Windows laptops), Windows silently virtualizes/
     * scales a DPI-unaware app's window, and the mouse coordinates SDL2
     * then reports can mismatch this editor's own fixed-pixel rendering
     * math (LINE_HEIGHT, the real x=12 left margin, etc., all assume
     * un-scaled real pixels) -- exactly the real, well-documented SDL2-
     * on-Windows gotcha this matches. Set BEFORE SDL_Init, a real,
     * standard SDL2 hint (a real, harmless no-op on Linux/macOS, so
     * left unconditional rather than #ifdef _WIN32-gated -- one fewer
     * platform-specific branch for identical real behavior everywhere
     * it doesn't apply). "permonitorv2" is SDL2's own documented
     * "preferred" level (Windows 10 1607+, falls back automatically to
     * the best available match on older Windows per SDL2's own docs). */
    SDL_SetHint(SDL_HINT_WINDOWS_DPI_AWARENESS, "permonitorv2");
    return SDL_Init(SDL_INIT_VIDEO) == 0 ? 0 : -1;
}

/* Real, confirmed-live bug fixed here (2026-08-27, founder real-time,
 * actually double-clicking a real file in the editor's own file-tree
 * sidebar: "no double click on nerd tree should open a new window it
 * just doesnt actually open the real file"). Traced end to end,
 * confirmed live, not guessed: spawn_new_instance's own real
 * fork+execl DOES correctly re-exec with the real target file as its
 * own argv[1] (verified directly -- the child's own real "editor:
 * loaded from <path>" line prints correctly). The real bug is here:
 * every window this program EVER creates -- the original one AND
 * every spawned instance -- used SDL_WINDOWPOS_CENTERED for both x
 * and y, so a second, spawned window opens in the EXACT same screen
 * position as the still-open first one, perfectly overlapping it.
 * Whether the new window lands behind or on top of the old one is
 * down to the window manager's own real stacking policy, but either
 * way it reads exactly like "nothing happened" -- the file WAS opened
 * in a real, correctly-loaded new window, just invisibly stacked
 * under (or indistinguishable from) the one already there. Fixed with
 * SDL_WINDOWPOS_UNDEFINED, the real, standard way to ask the window
 * manager for its OWN default placement instead of forcing dead
 * center -- every real desktop WM already staggers/cascades
 * successive undefined-position windows so they don't perfectly
 * overlap, the same real behavior every other multi-window desktop
 * app already relies on. */
static inline int sdl2_create_window_impl(const char *title, int w, int h) {
    if (g_sdl2_window_count >= SDL2_MAX_WINDOWS) return -1;
    SDL_Window *win = SDL_CreateWindow(title, SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                                        w, h, SDL_WINDOW_SHOWN);
    if (win == NULL) return -1;
    int handle = g_sdl2_window_count++;
    g_sdl2_windows[handle] = win;
    return handle;
}

static inline void sdl2_destroy_window_impl(int handle) {
    if (handle >= 0 && handle < g_sdl2_window_count && g_sdl2_windows[handle] != NULL) {
        SDL_DestroyWindow(g_sdl2_windows[handle]);
        g_sdl2_windows[handle] = NULL;
    }
}

static inline int sdl2_create_renderer_impl(int window_handle) {
    if (window_handle < 0 || window_handle >= g_sdl2_window_count
        || g_sdl2_windows[window_handle] == NULL) return -1;
    if (g_sdl2_renderer_count >= SDL2_MAX_RENDERERS) return -1;
    /* Accelerated first, software fallback -- this box's own real
     * Xvfb-backed verification run has no GPU, matching any other real
     * headless CI runner this might build on later. */
    SDL_Renderer *ren = SDL_CreateRenderer(g_sdl2_windows[window_handle], -1,
                                            SDL_RENDERER_ACCELERATED);
    if (ren == NULL) {
        ren = SDL_CreateRenderer(g_sdl2_windows[window_handle], -1, SDL_RENDERER_SOFTWARE);
    }
    if (ren == NULL) return -1;
    int handle = g_sdl2_renderer_count++;
    g_sdl2_renderers[handle] = ren;
    return handle;
}

static inline void sdl2_destroy_renderer_impl(int handle) {
    if (handle >= 0 && handle < g_sdl2_renderer_count && g_sdl2_renderers[handle] != NULL) {
        SDL_DestroyRenderer(g_sdl2_renderers[handle]);
        g_sdl2_renderers[handle] = NULL;
    }
}

static inline int sdl2_set_draw_color_impl(int renderer_handle, int r, int g, int b, int a) {
    if (renderer_handle < 0 || renderer_handle >= g_sdl2_renderer_count
        || g_sdl2_renderers[renderer_handle] == NULL) return -1;
    return SDL_SetRenderDrawColor(g_sdl2_renderers[renderer_handle],
                                   (Uint8)r, (Uint8)g, (Uint8)b, (Uint8)a) == 0 ? 0 : -1;
}

static inline int sdl2_render_clear_impl(int renderer_handle) {
    if (renderer_handle < 0 || renderer_handle >= g_sdl2_renderer_count
        || g_sdl2_renderers[renderer_handle] == NULL) return -1;
    return SDL_RenderClear(g_sdl2_renderers[renderer_handle]) == 0 ? 0 : -1;
}

static inline int sdl2_render_fill_rect_impl(int renderer_handle, int x, int y, int w, int h) {
    if (renderer_handle < 0 || renderer_handle >= g_sdl2_renderer_count
        || g_sdl2_renderers[renderer_handle] == NULL) return -1;
    SDL_Rect rect;
    rect.x = x; rect.y = y; rect.w = w; rect.h = h;
    return SDL_RenderFillRect(g_sdl2_renderers[renderer_handle], &rect) == 0 ? 0 : -1;
}

/* sdl2_render_set_scale_impl -- real Ctrl+Zoom support (2026-08-27,
 * founder real-time: "ctrl plus and ctrl minus and ctrl mous wheel
 * scoll should zoom just like pitviper" -- PITVIPER's own real,
 * already-shipped zoom feature, cmd/pitviper/main.go's zoomScale +
 * SDL_RenderSetScale, is the real, explicit model). `scale_percent`
 * (not a plain float) matches this whole runtime's own real "I32
 * everywhere at the PARENA-source boundary" convention -- 100 = real
 * 1.0x, 150 = real 1.5x -- converted to the real float SDL_
 * RenderSetScale actually wants only here, at the host-glue boundary. */
static inline int sdl2_render_set_scale_impl(int renderer_handle, int scale_percent) {
    if (renderer_handle < 0 || renderer_handle >= g_sdl2_renderer_count
        || g_sdl2_renderers[renderer_handle] == NULL) return -1;
    float scale = (float)scale_percent / 100.0f;
    return SDL_RenderSetScale(g_sdl2_renderers[renderer_handle], scale, scale) == 0 ? 0 : -1;
}

static inline void sdl2_render_present_impl(int renderer_handle) {
    if (renderer_handle >= 0 && renderer_handle < g_sdl2_renderer_count
        && g_sdl2_renderers[renderer_handle] != NULL) {
        SDL_RenderPresent(g_sdl2_renderers[renderer_handle]);
    }
}

/* poll-event -- v1 (2026-08-26, real keyboard-driven editing: founder
 * "continue working on parena editor"). Codes: 0 = no event pending,
 * 1 = Quit, 2 = KeyDown, 3 = TextInput, 4 = Other (every other real
 * SDL_EventType this program doesn't distinguish yet -- mouse motion,
 * window resize, real, separate, deferred follow-up once an editor loop
 * actually needs to react to them individually).
 *
 * A raw primitive can only return one plain scalar (no tuples -- VS0's
 * emitter doesn't support them, confirmed live), so KeyDown's real
 * keysym and TextInput's real typed text are read via two SEPARATE
 * follow-up raw calls (sdl2_last_event_key_impl/
 * sdl2_last_event_text_impl) right after poll-event reports which kind
 * fired -- the same "check a side-channel immediately after the call
 * that set it" shape io.prn's own raw-errno already establishes for
 * this exact reason. PITVIPER's own cmd/pitviper/main.go is the real
 * precedent for using BOTH SDL_KEYDOWN (special keys: backspace,
 * arrows, enter) and SDL_TEXTINPUT (real typed characters, correctly
 * handling shift/IME/etc. -- the actually-correct way to do text input
 * in SDL2, not raw keysym-to-ASCII mapping) together. */
static int g_sdl2_last_event_key = 0;
static char g_sdl2_last_event_text[32];
static int g_sdl2_last_mouse_x = 0;
static int g_sdl2_last_mouse_y = 0;
static char g_sdl2_last_drop_path[1024];
static int g_sdl2_last_wheel_delta = 0;

/* codes 5/6/7 -- real mouse event plumbing (2026-08-27, real mouse-
 * driven selection: click-to-position-cursor, click-drag-to-select --
 * the last real gap on this editor's own "still not done" list besides
 * redo-coalescing/glyph-atlas/macOS-dylib-bundling). Only SDL_BUTTON_
 * LEFT is reported as a real MouseDown/MouseUp -- right/middle-click
 * are a real, separate, deferred follow-up (this editor has no context
 * menu or paste-on-middle-click yet). x/y read via the same real
 * "two separate follow-up raw calls" shape KeyDown/TextInput already
 * establish (a raw primitive can only return one plain scalar). */
static inline int sdl2_poll_event_impl(void) {
    SDL_Event e;
    if (!SDL_PollEvent(&e)) return 0;
    if (e.type == SDL_QUIT) return 1;
    if (e.type == SDL_KEYDOWN) {
        g_sdl2_last_event_key = (int)e.key.keysym.sym;
        return 2;
    }
    if (e.type == SDL_TEXTINPUT) {
        size_t n = sizeof(g_sdl2_last_event_text) - 1;
        strncpy(g_sdl2_last_event_text, e.text.text, n);
        g_sdl2_last_event_text[n] = '\0';
        return 3;
    }
    if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
        g_sdl2_last_mouse_x = e.button.x;
        g_sdl2_last_mouse_y = e.button.y;
        return 5;
    }
    if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
        g_sdl2_last_mouse_x = e.button.x;
        g_sdl2_last_mouse_y = e.button.y;
        return 6;
    }
    if (e.type == SDL_MOUSEMOTION) {
        g_sdl2_last_mouse_x = e.motion.x;
        g_sdl2_last_mouse_y = e.motion.y;
        return 7;
    }
    /* code 8 -- real drag-and-drop-a-file-onto-the-window support
     * (2026-08-27, founder real-time: "i need an easy way to actually
     * open the files drag and drop onto the window"). e.drop.file is a
     * real SDL-malloc'd string the CALLER must free with SDL_free() --
     * copied out to a real, fixed-size buffer here (matching this
     * file's own established g_sdl2_last_event_text convention) and
     * freed immediately, so no SDL-owned pointer ever escapes into
     * PARENA code. SDL_DROPTEXT/DROPBEGIN/DROPCOMPLETE are real,
     * separate, deferred follow-ups (a real, honest v0: only real
     * FILE drops are handled, not a dragged text selection or the
     * begin/complete bracket events around a real multi-file drop --
     * each dropped file in a real multi-file drag still fires its own
     * real SDL_DROPFILE, so multi-file drops already work file-by-file
     * even without those). */
    if (e.type == SDL_DROPFILE) {
        size_t n = sizeof(g_sdl2_last_drop_path) - 1;
        strncpy(g_sdl2_last_drop_path, e.drop.file, n);
        g_sdl2_last_drop_path[n] = '\0';
        SDL_free(e.drop.file);
        return 8;
    }
    /* code 9 -- real mouse-wheel scroll support (2026-08-27, founder
     * real-time, actively using the editor: "mouse wheel scroll does
     * not work" -- scrolling had never been implemented at all, so any
     * real file taller than the window had no way to see past the
     * first screenful). e.wheel.y is real SDL2 convention: positive
     * "away from the user" (a real, physical wheel-forward/up motion),
     * negative "toward the user" -- handed straight through as a real
     * signed delta, the real scroll-direction convention decision
     * lives in the editor's own event-loop code, not here. */
    if (e.type == SDL_MOUSEWHEEL) {
        g_sdl2_last_wheel_delta = e.wheel.y;
        return 9;
    }
    return 4;
}

static inline int sdl2_last_event_key_impl(void) {
    return g_sdl2_last_event_key;
}

static inline char *sdl2_last_event_text_impl(Arena *dest) {
    size_t len = strlen(g_sdl2_last_event_text);
    char *out = (char *)arena_alloc(dest, len + 1);
    memcpy(out, g_sdl2_last_event_text, len + 1);
    return out;
}

static inline int sdl2_last_event_mouse_x_impl(void) {
    return g_sdl2_last_mouse_x;
}

static inline int sdl2_last_event_mouse_y_impl(void) {
    return g_sdl2_last_mouse_y;
}

static inline char *sdl2_last_event_drop_path_impl(Arena *dest) {
    size_t len = strlen(g_sdl2_last_drop_path);
    char *out = (char *)arena_alloc(dest, len + 1);
    memcpy(out, g_sdl2_last_drop_path, len + 1);
    return out;
}

static inline int sdl2_last_event_wheel_delta_impl(void) {
    return g_sdl2_last_wheel_delta;
}

static inline void sdl2_start_text_input_impl(void) {
    SDL_StartTextInput();
}

static inline void sdl2_stop_text_input_impl(void) {
    SDL_StopTextInput();
}

/* Real SDL keysym constants an editor's own keyboard loop actually
 * needs to distinguish -- SDLK_* values exposed as plain I32-returning
 * functions rather than PARENA-level constants (this language has no
 * const/#define-equivalent yet), matching the "thin FFI wrapper, not a
 * redesigned API" philosophy this whole file already follows. Narrow,
 * real set: whatever a real single-line text-edit loop needs
 * (backspace, enter, left/right for cursor movement) -- more added when
 * a real feature needs them, not speculatively here. */
static inline int sdl2_key_backspace_impl(void) { return SDLK_BACKSPACE; }
static inline int sdl2_key_return_impl(void) { return SDLK_RETURN; }
static inline int sdl2_key_left_impl(void) { return SDLK_LEFT; }
static inline int sdl2_key_right_impl(void) { return SDLK_RIGHT; }
/* Up/Down (2026-08-27, real, confirmed-live gap -- founder actually
 * using the editor: "left and right arrow work in the editor but up
 * down doesnt work with arrow keys" -- neither key was ever wired up
 * before this, only Left/Right/Home/End). */
static inline int sdl2_key_up_impl(void) { return SDLK_UP; }
static inline int sdl2_key_down_impl(void) { return SDLK_DOWN; }
/* Home/End/Delete (2026-08-26, real editor loop growth): the next real
 * keys a single-line edit loop needs once Left/Right cursor movement
 * exists -- jump to the start/end of the line, delete the character
 * AHEAD of the cursor (Backspace already covers behind it). */
static inline int sdl2_key_home_impl(void) { return SDLK_HOME; }
static inline int sdl2_key_end_impl(void) { return SDLK_END; }
static inline int sdl2_key_delete_impl(void) { return SDLK_DELETE; }
/* Tab (2026-08-27, founder real-time, actively using the editor: "tab
 * to indent doesnt work" -- SDL2 doesn't fire a real SDL_TEXTINPUT for
 * Tab (a real, standard GUI-toolkit convention: Tab is a navigation/
 * control key, not insertable text), so it needed its own real
 * KeyDown handling, same as Backspace/Return/Delete. */
static inline int sdl2_key_tab_impl(void) { return SDLK_TAB; }
/* F2/F3 -- real save/load keys (2026-08-26), a plain function-key
 * shortcut rather than a Ctrl+S-style modifier combo: real modifier-key
 * detection (SDL_Keymod) isn't wired up anywhere in this stdlib yet, a
 * real, separate, deferred feature, not needed for this real, honest v0. */
static inline int sdl2_key_f2_impl(void) { return SDLK_F2; }
static inline int sdl2_key_f3_impl(void) { return SDLK_F3; }

/* shift_held -- real modifier-key detection (2026-08-26, founder:
 * "continue working on parena editor" -- real text SELECTION, which
 * needs Shift+Left/Right). The gap flagged in the F2/F3 comment above
 * is closed here, but only for Shift, and via SDL_GetModState() (a
 * live query of the current keyboard state) rather than threading a
 * modifier field through poll-event's own KeyDown payload -- a raw
 * primitive can only return one plain scalar (no tuples), and
 * SDL_GetModState() is the real, standard SDL2 way to ask "is this
 * held right now" independent of which specific event just fired. */
static inline int sdl2_shift_held_impl(void) {
    return (SDL_GetModState() & KMOD_SHIFT) != 0;
}

/* ctrl_held / clipboard -- real clipboard integration (2026-08-27,
 * founder: "continue" -- the natural next real increment after text
 * SELECTION just shipped: copy/cut/paste). Same live-query shape as
 * shift_held above (SDL_GetModState() & KMOD_CTRL). Closes this file's
 * own sdl2.prn header comment's already-flagged "get-clipboard-text...
 * real, honest, NOT closed in this pass" gap.
 *
 * SDL_GetClipboardText() returns a real, SDL-malloc'd C string (never
 * NULL per SDL2's own docs -- an empty "" on no/unavailable clipboard
 * content, not a null pointer) that the CALLER must free with
 * SDL_free(); copied into the real PARENA arena here (matching every
 * other string-returning raw primitive in this file) and freed
 * immediately after, so no SDL-owned pointer ever escapes into PARENA
 * code. */
static inline int sdl2_ctrl_held_impl(void) {
    return (SDL_GetModState() & KMOD_CTRL) != 0;
}

/* gui_held -- real Cmd-key detection (2026-08-27, founder real-time:
 * Spotlight quick-open needs to fire on Ctrl+T on Windows/Linux OR
 * Cmd+T on macOS). SDL2's own KMOD_GUI is the real, portable modifier
 * mask for "the OS meta key" -- Cmd on macOS, the Windows/Super key
 * elsewhere -- same live SDL_GetModState() shape ctrl_held/shift_held
 * above already establish, not a new pattern.
 */
static inline int sdl2_gui_held_impl(void) {
    return (SDL_GetModState() & KMOD_GUI) != 0;
}

static inline void sdl2_set_clipboard_text_impl(const char *text) {
    SDL_SetClipboardText(text);
}

static inline char *sdl2_get_clipboard_text_impl(Arena *dest) {
    char *sdl_text = SDL_GetClipboardText();
    size_t len = sdl_text ? strlen(sdl_text) : 0;
    char *out = (char *)arena_alloc(dest, len + 1);
    if (len > 0) memcpy(out, sdl_text, len);
    out[len] = '\0';
    if (sdl_text) SDL_free(sdl_text);
    return out;
}

static inline int sdl2_get_ticks_impl(void) {
    return (int)SDL_GetTicks();
}

static inline void sdl2_delay_impl(int ms) {
    SDL_Delay((Uint32)ms);
}

static inline void sdl2_quit_impl(void) {
    SDL_Quit();
}

/* ---- stdlib/sdl2.prn real TEXT host glue (2026-08-26) -------------------
 * The concrete "next real extension once an editor loop actually needs
 * to render text" flagged when the renderer/draw-call gap was closed
 * earlier the same day -- real glyph/string rendering via SDL2_ttf,
 * PITVIPER's own real font backend (cmd/pitviper/main.go's shinyTexture,
 * "F11 shiny font... real JetBrains Mono via SDL2_ttf"). Font is a real
 * opaque I32 handle into a real host-side table, same reasoning as
 * Window/Renderer's own header comment above (a raw TTF_Font pointer
 * carried as a struct field is untested territory in this compiler, not
 * risked here).
 *
 * render-text is deliberately NOT a glyph-atlas/texture-cache system --
 * a real, honest v0: render the whole string to a fresh surface, blit
 * it, free both surface and texture immediately, every call. Correct,
 * simple, and reuses zero state across frames; real future work once an
 * editor loop's own frame budget actually needs per-glyph texture
 * caching (PITVIPER's own buildGlyphAtlas is the real precedent for
 * that, not attempted here). */
#define SDL2_MAX_FONTS 8
static TTF_Font *g_sdl2_fonts[SDL2_MAX_FONTS];
static int g_sdl2_font_count = 0;

static inline int sdl2_ttf_init_impl(void) {
    return TTF_Init() == 0 ? 0 : -1;
}

static inline void sdl2_ttf_quit_impl(void) {
    TTF_Quit();
}

static inline int sdl2_open_font_impl(const char *path, int point_size) {
    if (g_sdl2_font_count >= SDL2_MAX_FONTS) return -1;
    TTF_Font *f = TTF_OpenFont(path, point_size);
    if (f == NULL) return -1;
    int handle = g_sdl2_font_count++;
    g_sdl2_fonts[handle] = f;
    return handle;
}

static inline void sdl2_close_font_impl(int handle) {
    if (handle >= 0 && handle < g_sdl2_font_count && g_sdl2_fonts[handle] != NULL) {
        TTF_CloseFont(g_sdl2_fonts[handle]);
        g_sdl2_fonts[handle] = NULL;
    }
}

/* render-text -- renders `text` in `(r,g,b)` at `(x,y)` into the given
 * renderer, using the given font. Empty string is a real, honest no-op
 * (0, not an error) -- TTF_RenderUTF8_Blended itself fails on an empty
 * string, which is real SDL2_ttf behavior, not a bug this wrapper should
 * paper over as a fake success OR a fake failure; treating "nothing to
 * draw" as Ok matches how a real editor loop would want to call this
 * unconditionally per line, blank lines included, without a caller-side
 * special case. */
static inline int sdl2_render_text_impl(int renderer_handle, int font_handle, const char *text,
                                         int x, int y, int r, int g, int b) {
    if (renderer_handle < 0 || renderer_handle >= g_sdl2_renderer_count
        || g_sdl2_renderers[renderer_handle] == NULL) return -1;
    if (font_handle < 0 || font_handle >= g_sdl2_font_count
        || g_sdl2_fonts[font_handle] == NULL) return -1;
    if (text[0] == '\0') return 0;
    SDL_Color color;
    color.r = (Uint8)r; color.g = (Uint8)g; color.b = (Uint8)b; color.a = 255;
    SDL_Surface *surf = TTF_RenderUTF8_Blended(g_sdl2_fonts[font_handle], text, color);
    if (surf == NULL) return -1;
    SDL_Texture *tex = SDL_CreateTextureFromSurface(g_sdl2_renderers[renderer_handle], surf);
    int w = surf->w, h = surf->h;
    SDL_FreeSurface(surf);
    if (tex == NULL) return -1;
    SDL_Rect dst;
    dst.x = x; dst.y = y; dst.w = w; dst.h = h;
    int ok = SDL_RenderCopy(g_sdl2_renderers[renderer_handle], tex, NULL, &dst) == 0 ? 0 : -1;
    SDL_DestroyTexture(tex);
    return ok;
}

/* measure-text-width/height -- real glyph-cell sizing (PITVIPER's own
 * font.GlyphW/GlyphH, the numbers a real monospace terminal grid needs
 * to lay cells out correctly). No tuple return here -- VS0's emitter
 * does not support tuple return types yet (confirmed live: a real
 * "unsupported return type form" error trying one, not assumed), so
 * width and height are two separate real calls instead of one. */
static inline int sdl2_measure_text_width_impl(int font_handle, const char *text) {
    if (font_handle < 0 || font_handle >= g_sdl2_font_count || g_sdl2_fonts[font_handle] == NULL) return -1;
    int w = 0, h = 0;
    if (TTF_SizeUTF8(g_sdl2_fonts[font_handle], text, &w, &h) != 0) return -1;
    return w;
}

static inline int sdl2_measure_text_height_impl(int font_handle, const char *text) {
    if (font_handle < 0 || font_handle >= g_sdl2_font_count || g_sdl2_fonts[font_handle] == NULL) return -1;
    int w = 0, h = 0;
    if (TTF_SizeUTF8(g_sdl2_fonts[font_handle], text, &w, &h) != 0) return -1;
    return h;
}
#endif /* PARENA_NO_GRAPHICS */

/* ---- stdlib/json.prn's own real host-glue forward declaration (2026-08-28) --------------
 * json.prn's own json-unescape #target body calls host_json_unescape -- a real, per-HOST-
 * PROGRAM implementation (not a runtime primitive; the actual escape-decoding logic lives
 * entirely in whichever C program links json.prn in, same real division tests/test_json.c's
 * own header comment already documents), same real shape every mod's own `_host.h` file
 * already establishes for a callback INTO host code. Declared HERE (not in a separate
 * `_host.h`, unlike the REDGARDEN/ECOWAR mod convention) because every generated .c file
 * already `#include`s this header at its own top regardless of build shape -- a host program
 * that concatenates generated code AHEAD of its own real implementation (examples/
 * editor_main.c's own `cat gen.c editor_main.c`) needs this declaration reachable before that
 * real implementation appears later in the same translation unit, and this header is the one
 * real place guaranteed to already be there first. A host program that never links json.prn's
 * generated code never triggers a real "undefined reference" for this at link time -- a bare
 * forward declaration costs nothing there. Real implementations: examples/editor_main.c,
 * tests/test_json.c, tests/test_webdriver.c. */
extern char *host_json_unescape(char *s, int start, int end, char *out);

#endif /* PARENA_RUNTIME_H */
