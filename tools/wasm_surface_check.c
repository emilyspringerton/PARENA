/* tools/wasm_surface_check.c -- print or assert a wasm module's import/export surface, with no node.
 *
 *   wasm_surface_check <module.wasm>                         print every import and export
 *   wasm_surface_check <module.wasm> --imports F --exports G  exit 1 unless the module's surface is
 *                                                            EXACTLY the lines in F and G
 *
 * One line per entry, sorted, the same shape the spec files use:
 *   import env.reflux_host_dispatch func v(iiii)
 *   export level_tick func v()
 *   export memory memory -
 *
 * The decisions -- the binary format, what a signature is, how it prints -- are PARENA
 * (stdlib/wasm/surface.prn, compiled into tools/wasm_surface_gen.c by `make tools/wasm_surface_check`).
 * This file is plumbing: read the file, format lines, sort, compare. Founder real-time 2026-11-08
 * ("implement ... parena first"): the level build's old zero-import check ran only if node was
 * installed and printed "0 imports" either way; this replaces that with a check that always runs.
 */
#include "parena_runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "wasm_surface_gen.c"

#define MAX_LINES 4096
#define LINE_MAX_LEN 320

static const char *kind_name(int k) {
    switch (k) {
    case 0: return "func";
    case 1: return "table";
    case 2: return "memory";
    case 3: return "global";
    default: return "?";
    }
}

static void copy_name(Bytes d, int off, int len, char *out, int cap) {
    int k, n = (len < cap - 1) ? len : cap - 1;
    if (off < 0 || len < 0) n = 0;
    for (k = 0; k < n; k++) out[k] = (char)bytes_get_impl(d, off + k);
    out[n] = '\0';
}

static void sig_text(int sig, char *out) {
    int k = 0;
    if (sig == -1) { strcpy(out, "-"); return; }
    if (sig < 0) { strcpy(out, "?"); return; }
    for (;;) {
        int c = ws_sig_char(sig, k);
        if (c == 0 || k > 14) break;
        out[k++] = (char)c;
    }
    out[k] = '\0';
}

static int cmp_lines(const void *a, const void *b) { return strcmp((const char *)a, (const char *)b); }

static int read_file(const char *path, unsigned char **out, long *n) {
    FILE *f = fopen(path, "rb");
    long len;
    if (!f) return 0;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return 0; }
    len = ftell(f);
    if (len < 0 || fseek(f, 0, SEEK_SET) != 0) { fclose(f); return 0; }
    *out = (unsigned char *)malloc((size_t)len + 1);
    if (!*out) { fclose(f); return 0; }
    if (len > 0 && fread(*out, 1, (size_t)len, f) != (size_t)len) { fclose(f); free(*out); return 0; }
    fclose(f);
    *n = len;
    return 1;
}

/* the module's surface as sorted lines; -1 when the module is malformed */
static int surface_lines(Bytes d, char (*lines)[LINE_MAX_LEN]) {
    int ni = ws_import_count(d), ne = ws_export_count(d), i, n = 0;
    char mod[96], field[128], sig[24];
    if (ni < 0 || ne < 0 || ni + ne > MAX_LINES) return -1;
    for (i = 0; i < ni; i++) {
        copy_name(d, ws_import_module_off(d, i), ws_import_module_len(d, i), mod, (int)sizeof mod);
        copy_name(d, ws_import_field_off(d, i), ws_import_field_len(d, i), field, (int)sizeof field);
        sig_text(ws_import_sig(d, i), sig);
        snprintf(lines[n++], LINE_MAX_LEN, "import %s.%s %s %s", mod, field, kind_name(ws_import_kind(d, i)), sig);
    }
    for (i = 0; i < ne; i++) {
        copy_name(d, ws_export_name_off(d, i), ws_export_name_len(d, i), field, (int)sizeof field);
        sig_text(ws_export_sig(d, i), sig);
        snprintf(lines[n++], LINE_MAX_LEN, "export %s %s %s", field, kind_name(ws_export_kind(d, i)), sig);
    }
    qsort(lines, (size_t)n, LINE_MAX_LEN, cmp_lines);
    return n;
}

/* spec lines of one prefix ("import " / "export "), blank lines and '#' comments skipped */
static int spec_lines(const char *path, char (*lines)[LINE_MAX_LEN], int start) {
    FILE *f = fopen(path, "r");
    char buf[LINE_MAX_LEN];
    int n = start;
    if (!f) return -1;
    while (fgets(buf, (int)sizeof buf, f) && n < MAX_LINES) {
        size_t len = strlen(buf);
        while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r' || buf[len - 1] == ' ')) buf[--len] = '\0';
        if (len == 0 || buf[0] == '#') continue;
        strcpy(lines[n++], buf);
    }
    fclose(f);
    return n;
}

int main(int argc, char **argv) {
    static char got[MAX_LINES][LINE_MAX_LEN], want[MAX_LINES][LINE_MAX_LEN];
    const char *imports = NULL, *exports = NULL;
    unsigned char *raw = NULL;
    long len = 0, i;
    int ngot, nwant = 0, a, b, bad = 0;
    Arena arena;
    Bytes d;

    if (argc < 2) { fprintf(stderr, "usage: wasm_surface_check <module.wasm> [--imports F --exports G]\n"); return 2; }
    for (a = 2; a + 1 < argc; a += 2) {
        if (strcmp(argv[a], "--imports") == 0) imports = argv[a + 1];
        else if (strcmp(argv[a], "--exports") == 0) exports = argv[a + 1];
        else { fprintf(stderr, "wasm_surface_check: unknown flag %s\n", argv[a]); return 2; }
    }
    if (!read_file(argv[1], &raw, &len)) { fprintf(stderr, "wasm_surface_check: cannot read %s\n", argv[1]); return 2; }
    arena_init(&arena);
    d = bytes_alloc_impl(&arena, (int)len);
    for (i = 0; i < len; i++) bytes_set_impl(d, (int)i, raw[i]);
    free(raw);

    ngot = surface_lines(d, got);
    if (ngot < 0) { fprintf(stderr, "wasm_surface_check: %s is not a well-formed module\n", argv[1]); return 1; }
    if (!imports && !exports) {
        for (a = 0; a < ngot; a++) printf("%s\n", got[a]);
        return 0;
    }
    if (imports) nwant = spec_lines(imports, want, nwant);
    if (nwant >= 0 && exports) nwant = spec_lines(exports, want, nwant);
    if (nwant < 0) { fprintf(stderr, "wasm_surface_check: cannot read a spec file\n"); return 2; }
    qsort(want, (size_t)nwant, LINE_MAX_LEN, cmp_lines);

    /* sorted merge: report both directions */
    a = 0; b = 0;
    while (a < ngot || b < nwant) {
        int c = (a >= ngot) ? 1 : (b >= nwant) ? -1 : strcmp(got[a], want[b]);
        if (c == 0) { a++; b++; continue; }
        bad = 1;
        if (c < 0) printf("UNEXPECTED %s\n", got[a++]);
        else printf("MISSING    %s\n", want[b++]);
    }
    arena_free_all(&arena);
    if (bad) { fprintf(stderr, "wasm_surface_check: %s does not match its declared surface\n", argv[1]); return 1; }
    printf("surface ok: %s (%d entries)\n", argv[1], ngot);
    return 0;
}
