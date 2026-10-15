/* tests/test_v16_engine.c -- end-to-end verification of stdlib/v16/engine.prn (the V16 JS
 * execution engine spike). Runs every tests/v16/cases/NAME.js through the PARENA-compiled engine
 * and compares the console output byte-for-byte with tests/v16/cases/NAME.out, then checks the
 * engine's hard limits directly (step budget, arena budget, call depth) and that two runs in one
 * process do not leak into each other.
 *
 * The very same corpus is executed by tests/v16/run_wasm.py against the engine compiled to a
 * freestanding WebAssembly module, so "works natively" and "works in WASM" are one corpus.
 */
#include "parena_runtime.h"
#include <assert.h>
#include <dirent.h>
#include <stdio.h>
#include <string.h>

#include "test_v16_engine_gen.c"

static char *slurp(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    char *buf;
    long n;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    buf = (char *)malloc((size_t)n + 1);
    if (fread(buf, 1, (size_t)n, f) != (size_t)n) { free(buf); fclose(f); return NULL; }
    buf[n] = '\0';
    fclose(f);
    if (len) *len = (size_t)n;
    return buf;
}

static int run_case(const char *dir, const char *name) {
    char jspath[512], outpath[512];
    char *src, *want;
    const char *got;
    Arena a;
    int ok;
    snprintf(jspath, sizeof jspath, "%s/%s.js", dir, name);
    snprintf(outpath, sizeof outpath, "%s/%s.out", dir, name);
    src = slurp(jspath, NULL);
    want = slurp(outpath, NULL);
    if (!src || !want) { printf("FAIL: %s (missing .js/.out)\n", name); return 0; }
    arena_init(&a);
    got = v16_eval_bounded(src, 20000000, 4 * 1024 * 1024, &a);
    ok = strcmp(got, want) == 0;
    if (ok) printf("PASS: %s\n", name);
    else printf("FAIL: %s\n--- want ---\n%s--- got ---\n%s------------\n", name, want, got);
    arena_free_all(&a);
    free(src);
    free(want);
    return ok;
}

static const char *eval_with(const char *src, int steps, int budget, Arena *a) {
    return v16_eval_bounded((char *)src, steps, budget, a);
}

int main(int argc, char **argv) {
    const char *dir = argc > 1 ? argv[1] : "tests/v16/cases";
    DIR *d = opendir(dir);
    struct dirent *e;
    int total = 0, failed = 0;
    if (!d) { fprintf(stderr, "cannot open %s\n", dir); return 2; }
    while ((e = readdir(d)) != NULL) {
        size_t n = strlen(e->d_name);
        if (n > 3 && strcmp(e->d_name + n - 3, ".js") == 0) {
            char name[256];
            memcpy(name, e->d_name, n - 3);
            name[n - 3] = '\0';
            total++;
            if (!run_case(dir, name)) failed++;
        }
    }
    closedir(d);
    assert(total >= 10);

    /* limits, directly */
    {
        Arena a;
        arena_init(&a);
        assert(strstr(eval_with("while (true) {}", 100000, 1 << 20, &a), "step limit exceeded"));
        assert(strstr(eval_with("var s='x'; while(true){ s = s + s; }", 20000000, 1 << 20, &a), "arena budget exhausted"));
        assert(strstr(eval_with("function f(){ return f(); } f();", 20000000, 1 << 20, &a), "Maximum call stack"));
        assert(strcmp(eval_with("console.log(1+1)", 1000, 1 << 20, &a), "2\n") == 0);
        /* a failed run must not poison the next one */
        assert(strcmp(eval_with("console.log('again')", 1000, 1 << 20, &a), "again\n") == 0);
        arena_free_all(&a);
        printf("PASS: limits (steps, arena budget, call depth) and run isolation\n");
    }
    if (failed) { printf("%d of %d cases FAILED\n", failed, total); return 1; }
    printf("ALL %d corpus cases + limits PASS\n", total);
    return 0;
}
