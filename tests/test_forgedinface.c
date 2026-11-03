/* tests/test_forgedinface.c -- the thinnest possible C shim over FORGEDINFACE's own PARENA-native
 * checks (rig core, the GLB/VRM container, the GLB JSON decode, the software renderer). Every
 * real assertion lives in rig.prn / render.prn; this file calls each check once, prints the
 * individual results and asserts. parena-c has no `(defn main ...)` -> C `int main` emission
 * convention, so a standalone PARENA executable is not possible today -- the same boundary
 * tests/test_base4_pattern.c documents.
 */
#include "parena_runtime.h"
#include <assert.h>
#include <stdio.h>

#include <string.h>

/* Real host_json_unescape (not a stub) -- the same real implementation tests/test_json.c,
 * tests/test_textmate_loader.c and tests/test_webdriver.c already provide for this exact FFI
 * escape in stdlib/json.prn, including the same honest, documented, narrow \uXXXX-passthrough
 * limit those files' own header comments state. */
char *host_json_unescape(char *s, int start, int end, char *out) {
    int oi = 0;
    for (int i = start; i < end; i++) {
        if (s[i] == '\\' && i + 1 < end) {
            i++;
            switch (s[i]) {
                case '"': out[oi++] = '"'; break;
                case '\\': out[oi++] = '\\'; break;
                case '/': out[oi++] = '/'; break;
                case 'b': out[oi++] = '\b'; break;
                case 'f': out[oi++] = '\f'; break;
                case 'n': out[oi++] = '\n'; break;
                case 'r': out[oi++] = '\r'; break;
                case 't': out[oi++] = '\t'; break;
                case 'u':
                    for (int k = 0; k < 4 && i + 1 < end; k++) { i++; out[oi++] = s[i]; }
                    break;
                default: out[oi++] = s[i];
            }
        } else {
            out[oi++] = s[i];
        }
    }
    out[oi] = 0;
    return out;
}


#include "test_forgedinface_gen.c"

int main(void) {
    Arena arena;
    arena_init(&arena);

    int rig = self_test(&arena);
    int glb_json = self_test_glb_json(&arena);
    int render = self_test_render(&arena);
    int projection = self_test_projection(&arena);

    printf("rig=%d glb_json=%d render=%d projection=%d\n", rig, glb_json, render, projection);

    assert(rig);
    assert(glb_json);
    assert(render);
    assert(projection);

    printf("test_forgedinface: self-test (real PARENA-native test logic) passed\n");
    return 0;
}
