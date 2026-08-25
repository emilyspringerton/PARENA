/* tests/test_json.c — real end-to-end verification for stdlib/json.prn.
 * Not just "does it compile" -- actually calls the compiled parse()
 * against real JSON text and checks the resulting parsed structure is
 * correct, matching the corpus discipline stdlib/regex/syntax.prn's own
 * test file already established (well-formed + malformed cases).
 *
 * Real host_json_unescape implementation provided here (not a stub) --
 * enough to exercise the escape-decoding path for real, not left as an
 * untested "declared but not implemented" gap the way this stdlib's
 * pentest modules' own FFI bodies are. Deliberately narrow: handles the seven named
 * single-char escapes; \uXXXX passes the 4 hex digits through as raw
 * ASCII rather than real UTF-8 codepoint encoding -- a real, honest gap
 * flagged here, not the json.prn source's own claim (that comment
 * describes the eventual real host implementation's intended shape, not
 * what this test harness itself bothers to fully implement).
 */
#include "parena_runtime.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

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
                    /* real, honest narrow gap: passes the 4 hex digits
                     * through raw rather than decoding to UTF-8 */
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

#include "test_json_gen.c"

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s\n", msg); failures++; } \
    else { printf("PASS: %s\n", msg); } \
} while (0)

static Result do_parse(const char *text, Arena *a) {
    return parse((char *)text, a);
}

int main(void) {
    Arena a;
    arena_init(&a);

    /* --- well-formed cases --- */
    {
        Result r = do_parse("null", &a);
        CHECK(r.tag == 1, "null parses as Ok");
        JsonValue *v = (JsonValue *)r.value;
        CHECK(v->tag == JsonValue_TAG_JNull, "null produces JNull tag");
    }
    {
        Result r = do_parse("true", &a);
        CHECK(r.tag == 1, "true parses as Ok");
        JsonValue *v = (JsonValue *)r.value;
        CHECK(v->tag == JsonValue_TAG_JBool && *(int *)v->value == 1, "true produces JBool(1)");
    }
    {
        Result r = do_parse("false", &a);
        JsonValue *v = (JsonValue *)r.value;
        CHECK(r.tag == 1 && v->tag == JsonValue_TAG_JBool && *(int *)v->value == 0, "false produces JBool(0)");
    }
    {
        Result r = do_parse("42", &a);
        JsonValue *v = (JsonValue *)r.value;
        CHECK(r.tag == 1 && v->tag == JsonValue_TAG_JNumber && *(double *)v->value == 42.0, "integer 42 parses correctly");
    }
    {
        Result r = do_parse("-3.5e2", &a);
        JsonValue *v = (JsonValue *)r.value;
        CHECK(r.tag == 1 && v->tag == JsonValue_TAG_JNumber && *(double *)v->value == -350.0, "negative exponent float -3.5e2 == -350");
    }
    {
        Result r = do_parse("\"hello\"", &a);
        JsonValue *v = (JsonValue *)r.value;
        CHECK(r.tag == 1 && v->tag == JsonValue_TAG_JString && strcmp((char *)v->value, "hello") == 0, "simple string parses correctly");
    }
    {
        Result r = do_parse("\"a\\nb\\\"c\"", &a);
        JsonValue *v = (JsonValue *)r.value;
        CHECK(r.tag == 1 && v->tag == JsonValue_TAG_JString && strcmp((char *)v->value, "a\nb\"c") == 0, "string with \\n and \\\" escapes decodes correctly");
    }
    {
        Result r = do_parse("[]", &a);
        JsonValue *v = (JsonValue *)r.value;
        Vec *items = (Vec *)v->value;
        CHECK(r.tag == 1 && v->tag == JsonValue_TAG_JArray && items->count == 0, "empty array parses with 0 items");
    }
    {
        Result r = do_parse("[1, 2, 3]", &a);
        JsonValue *v = (JsonValue *)r.value;
        Vec *items = (Vec *)v->value;
        CHECK(r.tag == 1 && items->count == 3, "[1,2,3] parses with 3 items");
        JsonValue *e0 = (JsonValue *)items->items[0];
        JsonValue *e2 = (JsonValue *)items->items[2];
        CHECK(*(double *)e0->value == 1.0 && *(double *)e2->value == 3.0, "array elements have correct values in order");
    }
    {
        Result r = do_parse("[1, [2, 3], 4]", &a);
        JsonValue *v = (JsonValue *)r.value;
        Vec *outer = (Vec *)v->value;
        JsonValue *inner = (JsonValue *)outer->items[1];
        Vec *inner_items = (Vec *)inner->value;
        CHECK(r.tag == 1 && inner->tag == JsonValue_TAG_JArray && inner_items->count == 2, "nested array [1,[2,3],4] parses with correct nesting");
    }
    {
        Result r = do_parse("{}", &a);
        JsonValue *v = (JsonValue *)r.value;
        JsonValue_JObject_Payload *obj = (JsonValue_JObject_Payload *)v->value;
        CHECK(r.tag == 1 && v->tag == JsonValue_TAG_JObject && obj->keys.count == 0, "empty object parses with 0 keys");
    }
    {
        Result r = do_parse("{\"a\": 1, \"b\": [true, false]}", &a);
        JsonValue *v = (JsonValue *)r.value;
        JsonValue_JObject_Payload *obj = (JsonValue_JObject_Payload *)v->value;
        Vec keys = obj->keys;
        Vec values = obj->values;
        CHECK(r.tag == 1 && keys.count == 2, "object with 2 members parses with 2 keys");
        CHECK(strcmp((char *)keys.items[0], "a") == 0 && strcmp((char *)keys.items[1], "b") == 0, "object keys are in insertion order");
        JsonValue *bval = (JsonValue *)values.items[1];
        Vec *barr = (Vec *)bval->value;
        CHECK(barr->count == 2, "nested array value inside object parses correctly");
    }
    {
        Result r = do_parse("  {\"x\":  1}  ", &a);
        CHECK(r.tag == 1, "leading/trailing whitespace around a document is tolerated");
    }

    /* --- malformed cases: real errors, not crashes --- */
    {
        Result r = do_parse("", &a);
        CHECK(r.tag == 0, "empty input is a real parse error, not a crash");
    }
    {
        Result r = do_parse("{", &a);
        CHECK(r.tag == 0, "unterminated object is a real parse error");
    }
    {
        Result r = do_parse("[1, 2", &a);
        CHECK(r.tag == 0, "unterminated array is a real parse error");
    }
    {
        Result r = do_parse("nul", &a);
        CHECK(r.tag == 0, "truncated literal 'nul' is a real parse error, not a false-positive null");
    }
    {
        Result r = do_parse("\"unterminated", &a);
        CHECK(r.tag == 0, "unterminated string is a real parse error");
    }
    {
        Result r = do_parse("{\"a\": 1} garbage", &a);
        CHECK(r.tag == 0, "trailing garbage after a valid top-level value is rejected");
    }
    {
        Result r = do_parse("@", &a);
        CHECK(r.tag == 0, "an unrecognized leading character is a real parse error");
    }

    printf("\n%s (%d failures)\n", failures == 0 ? "ALL PASS" : "SOME FAILED", failures);
    return failures == 0 ? 0 : 1;
}
