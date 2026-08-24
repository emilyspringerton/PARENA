/* fmt.h — `parena fmt`, a gofmt-style re-indenter for .prn source.
 * See fmt.c's own header comment for why this is a standalone text-
 * level pass rather than an AST-based pretty-printer.
 */
#ifndef PARENA_FMT_H
#define PARENA_FMT_H

#include "arena.h"
#include <stddef.h>

/* Re-indents `src` (a NUL-terminated .prn source buffer) and returns a
 * freshly arena-allocated, NUL-terminated result. Never fails on
 * malformed input (unbalanced parens included) -- worst case, trailing
 * lines keep whatever depth the input left them at; a real diagnostic
 * belongs to `parena parse`, not to formatting. */
const char *fmt_source(Arena *arena, const char *src, size_t len);

#endif /* PARENA_FMT_H */
