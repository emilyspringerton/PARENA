/* tests/test_textmate_loader.c — real end-to-end verification for
 * stdlib/editor/textmate_loader.prn + stdlib/editor/lang_json.prn.
 * Not just "does it compile" -- actually loads a real .tmLanguage.json-shaped grammar,
 * builds real TmRule[]s from it, and tokenizes a real JSON line through them, checking
 * the real resulting token scopes and spans, same discipline test_json.c/test_editor_render.c
 * already establish for this stdlib.
 */
#include "parena_runtime.h"
#include <stdio.h>
#include <string.h>

/* Real host_json_unescape (not a stub) -- same real implementation test_json.c/
 * test_webdriver.c already establish for this exact FFI escape, same honest, documented,
 * narrow \uXXXX-passthrough limit those files' own header comments already state. */
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

#include "test_textmate_loader_gen.c"

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s\n", msg); failures++; } \
    else { printf("PASS: %s\n", msg); } \
} while (0)

int main(void) {
    Arena a;
    arena_init(&a);

    /* --- load_grammar: real, standalone loader tests (not lang_json-specific) --- */
    {
        Result r = load_grammar("{\"patterns\": [{\"match\": \"^x\", \"name\": \"keyword.x\"}]}", &a);
        CHECK(r.tag == 1, "load-grammar succeeds on a real, minimal, valid single-pattern grammar");
        if (r.tag == 1) {
            LoadResult *lr = (LoadResult *)r.value;
            CHECK(vec_len(&lr->rules) == 1, "the one real pattern became exactly one real TmRule");
            CHECK(lr->skipped == 0, "no real pattern entries were skipped");
        }
    }
    {
        Result r = load_grammar("{\"patterns\": [{\"begin\": \"/\\\\*\", \"end\": \"\\\\*/\"}, {\"match\": \"^y\", \"name\": \"keyword.y\"}]}", &a);
        CHECK(r.tag == 1, "load-grammar succeeds on a grammar mixing a begin/end entry with a real match entry");
        if (r.tag == 1) {
            LoadResult *lr = (LoadResult *)r.value;
            CHECK(vec_len(&lr->rules) == 1, "only the real match entry became a rule");
            CHECK(lr->skipped == 1, "the real begin/end entry was honestly counted as skipped, not silently dropped or crashed on");
        }
    }
    {
        Result r = load_grammar("not json at all", &a);
        CHECK(r.tag == 0, "load-grammar fails cleanly on real invalid JSON, not a crash");
    }
    {
        Result r = load_grammar("{\"nope\": []}", &a);
        CHECK(r.tag == 0, "load-grammar fails cleanly when the real document has no \"patterns\" key");
    }

    /* --- lang_json's own real grammar, real tokenization --- */
    {
        Result gr = build_json_grammar(&a);
        CHECK(gr.tag == 1, "lang_json's own real build-grammar succeeds");
        if (gr.tag == 1) {
            Vec rules = *(Vec *)gr.value;
            CHECK(vec_len(&rules) == 10, "lang_json's real grammar compiled all 10 real patterns into real TmRules");

            /* A real JSON object line: {"name": "tyler", "age": 12} */
            char *line = "{\"name\": \"tyler\", \"age\": 12}";
            Vec tokens = tokenize_line(&rules, line, &a);
            int n = vec_len(&tokens);
            CHECK(n > 0, "real tokenization of a real JSON line produced real tokens, not an empty result");

            /* Confirm the FIRST real token is the opening brace, correctly scoped. */
            if (n > 0) {
                Token *t0 = (Token *)vec_get(&tokens, 0);
                char first[2];
                first[0] = line[t0->start];
                first[1] = '\0';
                CHECK(strcmp(first, "{") == 0, "the real first token's own span is exactly the opening brace");
                CHECK(strcmp(t0->scope, "punctuation.structure.json") == 0, "the real opening-brace token got the real punctuation.structure.json scope");
            }

            /* Confirm at least one token has the real string scope, spanning a real quoted
             * string from the real source line (not a stale/placeholder value). */
            int found_string = 0;
            for (int i = 0; i < n; i++) {
                Token *t = (Token *)vec_get(&tokens, i);
                if (strcmp(t->scope, "string.quoted.double.json") == 0) {
                    int len = t->end - t->start;
                    char buf[64];
                    if (len > 0 && len < (int)sizeof buf) {
                        memcpy(buf, line + t->start, len);
                        buf[len] = '\0';
                        if (strcmp(buf, "\"name\"") == 0) { found_string = 1; break; }
                    }
                }
            }
            CHECK(found_string, "the real \"name\" property key was tokenized with the real string.quoted.double.json scope, spanning the real source bytes");

            /* Confirm a real number token exists with the correct real span. */
            int found_number = 0;
            for (int i = 0; i < n; i++) {
                Token *t = (Token *)vec_get(&tokens, i);
                if (strcmp(t->scope, "constant.numeric.json") == 0) {
                    int len = t->end - t->start;
                    char buf[16];
                    if (len > 0 && len < (int)sizeof buf) {
                        memcpy(buf, line + t->start, len);
                        buf[len] = '\0';
                        if (strcmp(buf, "12") == 0) { found_number = 1; break; }
                    }
                }
            }
            CHECK(found_number, "the real number 12 was tokenized with the real constant.numeric.json scope, spanning the real source bytes");
        }
    }

    printf("\n%d failures\n", failures);
    return failures == 0 ? 0 : 1;
}
