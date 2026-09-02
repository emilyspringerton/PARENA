/* tests/test_log_jsonl.c -- real end-to-end verification of stdlib/log/event.prn +
 * stdlib/log/jsonl.prn (LO FRAMEWORK_NORTHSTAR.md's own event-sourcing extension, founder
 * real-time: "continue building the framework with jsonl log streaming with mysql psql sqlite
 * etc projectors"). Confirms a real Event round-trips through a real file on disk: encode,
 * append, read back, re-parse via the existing real json/parse.
 */
#include "parena_runtime.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

/* Real host_json_unescape implementation -- same real, established convention every other real
 * caller of stdlib/json.prn's own #target host FFI companion already provides (test_json.c/
 * test_webdriver.c/test_textmate_loader.c/examples/editor_main.c all carry an identical copy).
 */
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

#include "test_log_jsonl_gen.c"

static char *field_str(JsonValue obj, const char *key) {
    Option field = get(obj, (char *)key);
    assert(field.tag == 1);
    Option s = as_string(*(JsonValue *)field.value);
    assert(s.tag == 1);
    return (char *)s.value;
}

int main(void) {
    Arena arena;
    arena_init(&arena);

    const char *path = "/tmp/test_log_jsonl.jsonl";
    unlink(path); /* real, clean slate -- a leftover file from a prior run must not affect this one */

    /* Real, honest empty-Vec case: reading a log that doesn't exist yet is not an error. */
    Result r = read_lines((char *)path, &arena);
    assert(r.tag == 1);
    Vec lines = *(Vec *)r.value;
    assert(vec_len(&lines) == 0);

    /* Two real events, appended in order. */
    Event e1 = {"Repo", "repo-1", "create", "{\"name\":\"PARENA\"}", 1000};
    Result a1 = append_event_((char *)path, &e1, &arena);
    assert(a1.tag == 1);

    Event e2 = {"Repo", "repo-1", "update", "{\"name\":\"PARENA2\"}", 2000};
    Result a2 = append_event_((char *)path, &e2, &arena);
    assert(a2.tag == 1);

    /* Read back: two real lines, in the same order they were appended. */
    Result r2 = read_lines((char *)path, &arena);
    assert(r2.tag == 1);
    Vec lines2 = *(Vec *)r2.value;
    assert(vec_len(&lines2) == 2);

    /* Line 1 round-trips through the existing real json/parse, matching e1 exactly. */
    Result p1 = parse((char *)vec_get(&lines2, 0), &arena);
    assert(p1.tag == 1);
    JsonValue obj1 = *(JsonValue *)p1.value;
    assert(strcmp(field_str(obj1, "kind"), "Repo") == 0);
    assert(strcmp(field_str(obj1, "id"), "repo-1") == 0);
    assert(strcmp(field_str(obj1, "op"), "create") == 0);
    Option fields1 = get(obj1, "fields");
    assert(fields1.tag == 1);
    assert(strcmp(field_str(*(JsonValue *)fields1.value, "name"), "PARENA") == 0);

    /* Line 2 -- confirms real append (not overwrite) and a real second event's own values. */
    Result p2 = parse((char *)vec_get(&lines2, 1), &arena);
    assert(p2.tag == 1);
    JsonValue obj2 = *(JsonValue *)p2.value;
    assert(strcmp(field_str(obj2, "op"), "update") == 0);
    Option fields2 = get(obj2, "fields");
    assert(fields2.tag == 1);
    assert(strcmp(field_str(*(JsonValue *)fields2.value, "name"), "PARENA2") == 0);

    /* Real quote/backslash escaping round-trips correctly through json-escape-string. */
    Event e3 = {"Repo", "repo-2", "create", "{}", 3000};
    e3.kind = "Weird \"quoted\" \\slash";
    Result a3 = append_event_((char *)path, &e3, &arena);
    assert(a3.tag == 1);
    Result r3 = read_lines((char *)path, &arena);
    assert(r3.tag == 1);
    Vec lines3 = *(Vec *)r3.value;
    assert(vec_len(&lines3) == 3);
    Result p3 = parse((char *)vec_get(&lines3, 2), &arena);
    assert(p3.tag == 1);
    assert(strcmp(field_str(*(JsonValue *)p3.value, "kind"), "Weird \"quoted\" \\slash") == 0);

    unlink(path);
    printf("test_log_jsonl: all assertions passed\n");
    return 0;
}
