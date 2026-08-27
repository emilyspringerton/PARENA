/* runtime/prnfmt_bridge.c -- real bridge between the parena COMPILER's
 * own fmt_source() (src/fmt.c, the real, already-working `parena fmt`
 * command) and the editor's own real save-time auto-format (2026-08-27,
 * founder real-time: "build prnfmt into the editor so saving files
 * auto formats them").
 *
 * Real, confirmed-live reason this is its own separate translation
 * unit rather than a plain #include into examples/editor_main.c: both
 * src/arena.h (the compiler's own bump allocator, for AST nodes/
 * codegen work) and runtime/parena_runtime.h (the RUNTIME's own,
 * separate bump allocator, for compiled PARENA programs) declare their
 * own, independently-defined `typedef struct { ... } Arena;` -- real,
 * structurally similar (both a bump-allocator block list) but NOT the
 * same type, and C doesn't allow two conflicting definitions of the
 * same typedef name in one translation unit. This file includes ONLY
 * the compiler's own arena.h/fmt.h, keeping that Arena type fully
 * contained here; its own public function below uses nothing but plain
 * `char *`/`size_t`, so no Arena type of EITHER kind ever crosses the
 * boundary into examples/editor_main.c, which only ever sees a plain
 * forward declaration (no header include needed there at all). */
#include "../src/arena.h"
#include "../src/fmt.h"
#include <stdlib.h>
#include <string.h>

/* prnfmt_format_and_copy -- runs `src` through the real parena
 * formatter and returns a freshly malloc'd, NUL-terminated copy of the
 * result (the caller's own responsibility to free() -- a plain malloc,
 * matching neither Arena's own allocator, since this crosses back out
 * of the compiler's own short-lived local Arena before that Arena is
 * torn down). Returns NULL only on a real, genuine allocation failure
 * -- fmt_source() itself never fails on malformed input (see fmt.h's
 * own header comment: worst case, trailing lines keep whatever depth
 * the input left them at). */
char *prnfmt_format_and_copy(const char *src, size_t len) {
    Arena a;
    arena_init(&a);
    const char *formatted = fmt_source(&a, src, len);
    size_t flen = strlen(formatted);
    char *out = (char *)malloc(flen + 1);
    if (out) memcpy(out, formatted, flen + 1);
    arena_free_all(&a);
    return out;
}
