/* tests/test_yaml.c — real end-to-end verification for stdlib/yaml.prn.
 * Actually calls the compiled parse() against real YAML text (including
 * a representative sample shaped like this monorepo's own real .yml
 * files, not just synthetic minimal cases) and checks the resulting
 * parsed structure, matching test_json.c's own established discipline.
 */
#include "parena_runtime.h"
#include "test_yaml_gen.c"
#include <stdio.h>
#include <string.h>

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

    /* --- scalars at top level --- */
    {
        Result r = do_parse("null", &a);
        YamlValue *v = (YamlValue *)r.value;
        CHECK(r.tag == 1 && v->tag == YamlValue_TAG_YNull, "bare 'null' parses as YNull");
    }
    {
        Result r = do_parse("42", &a);
        YamlValue *v = (YamlValue *)r.value;
        CHECK(r.tag == 1 && v->tag == YamlValue_TAG_YNumber && *(double *)v->value == 42.0, "bare '42' parses as YNumber(42)");
    }
    {
        Result r = do_parse("true", &a);
        YamlValue *v = (YamlValue *)r.value;
        CHECK(r.tag == 1 && v->tag == YamlValue_TAG_YBool && *(int *)v->value == 1, "bare 'true' parses as YBool(1)");
    }

    /* --- block mapping, simple --- */
    {
        const char *text = "name: fatbaby\nage: 5\nactive: true\n";
        Result r = do_parse(text, &a);
        YamlValue *v = (YamlValue *)r.value;
        CHECK(r.tag == 1 && v->tag == YamlValue_TAG_YMap, "simple 3-key mapping parses as Ok/YMap");
        YamlValue_YMap_Payload *m = (YamlValue_YMap_Payload *)v->value;
        CHECK(m->keys.count == 3, "3-key mapping has 3 keys");
        CHECK(strcmp((char *)m->keys.items[0], "name") == 0
              && strcmp((char *)m->keys.items[1], "age") == 0
              && strcmp((char *)m->keys.items[2], "active") == 0,
              "mapping keys are in real document order");
        YamlValue *name_val = (YamlValue *)m->values.items[0];
        CHECK(name_val->tag == YamlValue_TAG_YString && strcmp((char *)name_val->value, "fatbaby") == 0, "string value 'fatbaby' parses correctly");
        YamlValue *age_val = (YamlValue *)m->values.items[1];
        CHECK(age_val->tag == YamlValue_TAG_YNumber && *(double *)age_val->value == 5.0, "numeric value 5 parses correctly");
        YamlValue *active_val = (YamlValue *)m->values.items[2];
        CHECK(active_val->tag == YamlValue_TAG_YBool && *(int *)active_val->value == 1, "boolean value true parses correctly");
    }

    /* --- nested mapping (real bukkit.yml shape) --- */
    {
        const char *text =
            "settings:\n"
            "  allow-end: true\n"
            "  connection-throttle: 4000\n"
            "  shutdown-message: Server closed\n"
            "spawn-limits:\n"
            "  monsters: 70\n"
            "  animals: 10\n";
        Result r = do_parse(text, &a);
        YamlValue *v = (YamlValue *)r.value;
        CHECK(r.tag == 1 && v->tag == YamlValue_TAG_YMap, "real bukkit.yml-shaped nested mapping parses as Ok/YMap");
        YamlValue_YMap_Payload *top = (YamlValue_YMap_Payload *)v->value;
        CHECK(top->keys.count == 2, "top-level has 2 keys (settings, spawn-limits)");
        YamlValue *settings = (YamlValue *)top->values.items[0];
        CHECK(settings->tag == YamlValue_TAG_YMap, "'settings' value is itself a nested mapping");
        YamlValue_YMap_Payload *sm = (YamlValue_YMap_Payload *)settings->value;
        CHECK(sm->keys.count == 3, "nested 'settings' mapping has 3 real keys");
        YamlValue *msg = (YamlValue *)sm->values.items[2];
        CHECK(msg->tag == YamlValue_TAG_YString && strcmp((char *)msg->value, "Server closed") == 0,
              "unquoted multi-word plain scalar 'Server closed' parses as one string");
    }

    /* --- block sequence --- */
    {
        const char *text = "- alpha\n- beta\n- gamma\n";
        Result r = do_parse(text, &a);
        YamlValue *v = (YamlValue *)r.value;
        CHECK(r.tag == 1 && v->tag == YamlValue_TAG_YSeq, "3-item block sequence parses as Ok/YSeq");
        Vec *items = (Vec *)v->value;
        CHECK(items->count == 3, "sequence has 3 items");
        CHECK(strcmp((char *)((YamlValue *)items->items[0])->value, "alpha") == 0
              && strcmp((char *)((YamlValue *)items->items[2])->value, "gamma") == 0,
              "sequence items are in order with correct values");
    }

    /* --- sequence nested under a mapping key --- */
    {
        const char *text = "fruits:\n  - apple\n  - banana\ncount: 2\n";
        Result r = do_parse(text, &a);
        YamlValue *v = (YamlValue *)r.value;
        YamlValue_YMap_Payload *m = (YamlValue_YMap_Payload *)v->value;
        CHECK(r.tag == 1 && m->keys.count == 2, "mapping with a nested sequence value has 2 top-level keys");
        YamlValue *fruits = (YamlValue *)m->values.items[0];
        CHECK(fruits->tag == YamlValue_TAG_YSeq, "'fruits' value is a nested sequence");
        Vec *fruit_items = (Vec *)fruits->value;
        CHECK(fruit_items->count == 2 && strcmp((char *)((YamlValue *)fruit_items->items[1])->value, "banana") == 0,
              "nested sequence under a mapping key has correct items");
    }

    /* --- comments and blank lines are ignored --- */
    {
        const char *text =
            "# this is a full-line comment\n"
            "\n"
            "key: value  # trailing comment\n"
            "\n"
            "other: 1\n";
        Result r = do_parse(text, &a);
        YamlValue *v = (YamlValue *)r.value;
        YamlValue_YMap_Payload *m = (YamlValue_YMap_Payload *)v->value;
        CHECK(r.tag == 1 && m->keys.count == 2, "comments and blank lines are skipped, real content still parses");
        YamlValue *keyval = (YamlValue *)m->values.items[0];
        CHECK(strcmp((char *)keyval->value, "value") == 0, "trailing comment is stripped from the scalar value");
    }

    /* --- quoted strings --- */
    {
        const char *text = "msg: \"hello world\"\n";
        Result r = do_parse(text, &a);
        YamlValue *v = (YamlValue *)r.value;
        YamlValue_YMap_Payload *m = (YamlValue_YMap_Payload *)v->value;
        YamlValue *msg = (YamlValue *)m->values.items[0];
        CHECK(r.tag == 1 && strcmp((char *)msg->value, "hello world") == 0, "double-quoted string value has its quotes stripped");
    }

    /* --- null variants --- */
    {
        const char *text = "a: ~\nb: null\nc:\n";
        Result r = do_parse(text, &a);
        YamlValue *v = (YamlValue *)r.value;
        YamlValue_YMap_Payload *m = (YamlValue_YMap_Payload *)v->value;
        YamlValue *a_val = (YamlValue *)m->values.items[0];
        YamlValue *b_val = (YamlValue *)m->values.items[1];
        YamlValue *c_val = (YamlValue *)m->values.items[2];
        CHECK(r.tag == 1 && a_val->tag == YamlValue_TAG_YNull, "'~' parses as YNull");
        CHECK(b_val->tag == YamlValue_TAG_YNull, "'null' parses as YNull");
        CHECK(c_val->tag == YamlValue_TAG_YNull, "an empty value ('c:' with nothing after) parses as YNull");
    }

    /* --- bare scalar document (no mapping/sequence wrapper at all) --- */
    {
        Result r = do_parse("just a plain string\n", &a);
        YamlValue *v = (YamlValue *)r.value;
        CHECK(r.tag == 1 && v->tag == YamlValue_TAG_YString && strcmp((char *)v->value, "just a plain string") == 0,
              "a document with no ':' and no '-' at all is a valid bare scalar, not an error");
    }

    /* --- malformed: real error, not a crash --- */
    {
        /* first line establishes a real mapping (has ':'); a later line
         * at the SAME indent with no ':' is a genuine structural error,
         * not a second bare-scalar document. */
        Result r = do_parse("a: 1\nno colon here\n", &a);
        CHECK(r.tag == 0, "a colonless line appearing mid-mapping (not as the block's first line) is a real parse error");
    }

    printf("\n%s (%d failures)\n", failures == 0 ? "ALL PASS" : "SOME FAILED", failures);
    return failures == 0 ? 0 : 1;
}
