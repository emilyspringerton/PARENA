/* tests/test_wasm_surface.c -- stdlib/wasm/surface.prn against a hand-assembled module
 * (tests/test_wasm_surface_fixture.h): imports of every kind the reader distinguishes, a memory
 * limit with a max, a custom section between the known ones, a re-exported import (function index
 * space order), packed signatures and their wasm3-notation text, and -- the property the host
 * relies on -- every truncation of the module and a corrupted one answer -1/-2 instead of reading
 * past the buffer. The real-module check (an LLVM-built level) is the level build's own
 * surface assertion, scripts/build_brawlpit_level_wasm.sh. */
#include "parena_runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "test_wasm_surface_gen.c"
#include "test_wasm_surface_fixture.h"

static int failures = 0;
#define CHECK(c, m) do { if (!(c)) { printf("FAIL: %s\n", m); failures++; } else printf("PASS: %s\n", m); } while (0)

static Bytes load(Arena *a, const unsigned char *d, unsigned int n) {
    Bytes b = bytes_alloc_impl(a, (int)n);
    unsigned int i;
    for (i = 0; i < n; i++) bytes_set_impl(b, (int)i, d[i]);
    return b;
}

static Bytes cstr(Arena *a, const char *s) { return load(a, (const unsigned char *)s, (unsigned int)strlen(s)); }

static int name_eq(Bytes d, int off, int len, const char *want) {
    int k;
    if (off < 0 || len != (int)strlen(want)) return 0;
    for (k = 0; k < len; k++) if (bytes_get_impl(d, off + k) != (unsigned char)want[k]) return 0;
    return 1;
}

static void sig_text(int sig, char *out) {
    int k = 0;
    for (;;) {
        int c = ws_sig_char(sig, k);
        if (c == 0 || k > 14) break;
        out[k++] = (char)c;
    }
    out[k] = '\0';
}

int main(void) {
    Arena a;
    char buf[32];
    Bytes d, junk;
    unsigned int n;
    int all_bounded = 1;
    arena_init(&a);
    d = load(&a, fx_mod, fx_mod_len);

    CHECK(ws_valid_(d), "the fixture is a valid module header");
    CHECK(ws_import_count(d) == 4, "4 imports");
    CHECK(ws_import_kind(d, 0) == 0 && ws_import_kind(d, 1) == 2 && ws_import_kind(d, 2) == 0 && ws_import_kind(d, 3) == 3,
          "import kinds: func, memory, func, global");
    CHECK(name_eq(d, ws_import_module_off(d, 0), ws_import_module_len(d, 0), "env"), "import 0 module is env");
    CHECK(name_eq(d, ws_import_field_off(d, 0), ws_import_field_len(d, 0), "reflux_host_dispatch"), "import 0 field");
    CHECK(name_eq(d, ws_import_field_off(d, 1), ws_import_field_len(d, 1), "memory"), "import 1 (memory) field");
    CHECK(name_eq(d, ws_import_field_off(d, 2), ws_import_field_len(d, 2), "reflux_host_total_dispatched"),
          "import 2 field, found past a memory limit that has a max");
    CHECK(name_eq(d, ws_import_field_off(d, 3), ws_import_field_len(d, 3), "g"), "import 3 (global) field");
    sig_text(ws_import_sig(d, 0), buf);
    CHECK(strcmp(buf, "v(iiii)") == 0, "import 0 signature text is v(iiii)");
    sig_text(ws_import_sig(d, 2), buf);
    CHECK(strcmp(buf, "i()") == 0, "import 2 signature text is i()");
    CHECK(ws_import_sig(d, 1) == -1 && ws_import_sig(d, 3) == -1, "non-function imports have no signature (-1)");
    CHECK(ws_import_func_count(d) == 2, "2 function imports");
    CHECK(ws_import_sig(d, 0) == ws_sig_of(0, 4, (1 << 7) | (1 << 10) | (1 << 13) | (1 << 16)),
          "ws-sig-of builds the same packed v(iiii) a host would offer");

    CHECK(ws_export_count(d) == 4, "4 exports, read past a custom section");
    CHECK(name_eq(d, ws_export_name_off(d, 0), ws_export_name_len(d, 0), "level_api"), "export 0 name");
    CHECK(name_eq(d, ws_export_name_off(d, 3), ws_export_name_len(d, 3), "reexport"), "export 3 name");
    CHECK(ws_export_kind(d, 2) == 2 && ws_export_index(d, 2) == 0, "export 2 is memory 0");
    sig_text(ws_export_sig(d, 0), buf);
    CHECK(strcmp(buf, "i()") == 0, "export 0 (function 2 = first defined, type 1) is i()");
    sig_text(ws_export_sig(d, 1), buf);
    CHECK(strcmp(buf, "v(F)") == 0, "export 1 (type 2) is v(F)");
    sig_text(ws_export_sig(d, 3), buf);
    CHECK(strcmp(buf, "v(iiii)") == 0, "a re-exported import resolves through the import's type");
    CHECK(ws_export_sig(d, 2) == -1, "a memory export has no signature");
    CHECK(ws_import_kind(d, 4) == -1 && ws_export_name_off(d, 9) == -1 && ws_import_kind(d, -1) == -1,
          "out-of-range indexes answer -1");
    CHECK(ws_name_is_(d, ws_export_name_off(d, 1), ws_export_name_len(d, 1), cstr(&a, "bp_frame")) &&
          !ws_name_is_(d, ws_export_name_off(d, 1), ws_export_name_len(d, 1), cstr(&a, "bp_frames")),
          "ws-name-is? is an exact byte match");

    /* every truncation: counts/kinds/sigs stay in range, never a crash (bytes_get_impl would fault
       under ASan on an out-of-range read if the walk were unbounded) */
    for (n = 0; n < fx_mod_len; n++) {
        Bytes t = load(&a, fx_mod, n);
        int ic = ws_import_count(t), ec = ws_export_count(t), i;
        if (ic < -1 || ic > 4 || ec < -1 || ec > 4) all_bounded = 0;
        for (i = 0; i < 4; i++) {
            (void)ws_import_kind(t, i); (void)ws_import_sig(t, i); (void)ws_import_field_off(t, i);
            (void)ws_export_sig(t, i); (void)ws_export_name_len(t, i);
        }
    }
    CHECK(all_bounded, "every truncation of the module answers in range without reading past it");

    junk = load(&a, fx_mod, fx_mod_len);
    bytes_set_impl(junk, 1, 0x62);
    CHECK(!ws_valid_(junk) && ws_import_count(junk) == -1 && ws_export_count(junk) == -1, "bad magic: -1 everywhere");
    junk = load(&a, fx_mod, fx_mod_len);
    bytes_set_impl(junk, 9, 0xff); /* the type section's size LEB now never terminates inside 5 bytes */
    CHECK(ws_import_count(junk) == -1, "a section size that runs off the end is malformed (-1)");

    arena_free_all(&a);
    if (failures) { printf("%d FAILED\n", failures); return 1; }
    printf("ALL PASS\n");
    return 0;
}
