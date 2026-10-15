/* examples/v16_wasm/v16_host.c -- the slim C host around the PARENA-compiled V16 JS engine
 * (stdlib/v16/engine.prn). It owns exactly three things the engine cannot: the static input
 * buffer the embedder writes the script into, the Arena the engine allocates from, and `main()`
 * for the native build. Everything else -- lexing, compiling, executing, string/array/object
 * memory, console output -- is PARENA.
 *
 * Build (see Makefile target `v16-wasm`): parena build stdlib/string.prn stdlib/v16/engine.prn
 * -o v16_engine_gen.c, then compile this file with that generated C included via
 * -DV16_ENGINE_GEN='"v16_engine_gen.c"'.
 *
 * WASM exports (the whole embedder ABI; no WASI imports are needed at run time):
 *   char *v16_input(void)          pointer to the script buffer (UTF-8/ASCII bytes)
 *   int   v16_input_capacity(void) its size in bytes
 *   int   v16_run(int len)         compile+run the first `len` bytes; returns output length
 *   char *v16_output(void)         NUL-terminated console output (+ "Uncaught <error>" on failure)
 *   int   v16_arena_used(void)     bytes the last run took from the arena pool (high-water)
 */
#include "parena_runtime.h"
#include V16_ENGINE_GEN

#ifndef V16_BUDGET
#define V16_BUDGET (32 * 1024 * 1024) /* bytes of arena a script may consume (native default) */
#endif
#ifndef V16_STEPS
#define V16_STEPS 20000000 /* VM instruction budget per run */
#endif
#ifndef V16_INPUT_MAX
#define V16_INPUT_MAX (256 * 1024)
#endif

#ifdef __wasm__
#define V16_EXPORT(n) __attribute__((export_name(n)))
#else
#define V16_EXPORT(n)
#endif

static char g_input[V16_INPUT_MAX + 1];
static const char *g_output = "";
static Arena g_arena;
static int g_arena_ready = 0;

V16_EXPORT("v16_input") char *v16_input(void) { return g_input; }
V16_EXPORT("v16_input_capacity") int v16_input_capacity(void) { return V16_INPUT_MAX; }

V16_EXPORT("v16_run") int v16_run(int len) {
    if (len < 0) len = 0;
    if (len > V16_INPUT_MAX) len = V16_INPUT_MAX;
    g_input[len] = '\0';
    if (g_arena_ready) arena_free_all(&g_arena); /* one run == one region: drop the last one */
    arena_init(&g_arena);
    g_arena_ready = 1;
    g_output = v16_eval_bounded(g_input, V16_STEPS, V16_BUDGET, &g_arena);
    return (int)strlen(g_output);
}

V16_EXPORT("v16_output") const char *v16_output(void) { return g_output; }
V16_EXPORT("v16_arena_used") int v16_arena_used(void) { return (int)parena_static_pool_high_water(); }

#ifndef __wasm__
int main(int argc, char **argv) {
    FILE *f = argc > 1 ? fopen(argv[1], "rb") : stdin;
    size_t n;
    if (!f) { fprintf(stderr, "v16: cannot open %s\n", argv[1]); return 2; }
    n = fread(g_input, 1, V16_INPUT_MAX, f);
    if (f != stdin) fclose(f);
    v16_run((int)n);
    fputs(g_output, stdout);
    return strstr(g_output, "Uncaught ") ? 1 : 0;
}
#endif
