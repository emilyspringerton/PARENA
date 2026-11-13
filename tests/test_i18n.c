/* test_i18n.c -- real verification for the translation core module (src/i18n.c).
 *
 * Every expected value below was worked out by hand from the contract in src/i18n.h, not copied from
 * the program's own output. The last block is end to end: a `tr` goes through parse -> i18n_lower ->
 * region analysis -> the real Lua emitter, and the emitted text must carry the catalogue's string.
 */
#include "../src/arena.h"
#include "../src/ast.h"
#include "../src/emit_lua.h"
#include "../src/i18n.h"
#include "../src/parser.h"
#include "../src/region.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, msg) do { \
    if (cond) { g_pass++; printf("PASS: %s\n", msg); } \
    else { g_fail++; printf("FAIL: %s\n", msg); } \
} while (0)

static Node *parse(Arena *a, const char *src) {
    const char *err = NULL;
    Node *p = parse_program(a, src, strlen(src), &err);
    if (!p) printf("  (parse error: %s)\n", err ? err : "?");
    return p;
}

static int load(Arena *a, const char *src, I18nCatalog *out) {
    const char *err = i18n_load_catalog(a, src, strlen(src), out);
    if (err) printf("  (catalogue error: %s)\n", err);
    return err == NULL;
}

int main(void) {
    /* --- ids --- */
    {
        Arena a;
        arena_init(&a);
        const char *x = i18n_derive_id(&a, "Hello", 5);
        const char *y = i18n_derive_id(&a, "Hello", 5);
        const char *z = i18n_derive_id(&a, "Hellp", 5);
        CHECK(strcmp(x, y) == 0, "the derived id is stable for the same text");
        CHECK(strcmp(x, z) != 0, "a different text gets a different id");
        CHECK(strlen(x) == 18 && strncmp(x, "s_", 2) == 0, "a derived id is s_ plus 16 hex digits");
        /* FNV-1a 64 of the empty string is its offset basis, 0xcbf29ce484222325. */
        CHECK(strcmp(i18n_derive_id(&a, "", 0), "s_cbf29ce484222325") == 0,
              "FNV-1a of the empty text is the published offset basis");
        /* FNV-1a 64 of "a" is 0xaf63dc4c8601ec8c (published test vector). */
        CHECK(strcmp(i18n_derive_id(&a, "a", 1), "s_af63dc4c8601ec8c") == 0,
              "FNV-1a of \"a\" matches the published test vector");
        arena_free_all(&a);
    }

    /* --- module off: tr becomes its inline default, at any depth --- */
    {
        Arena a;
        arena_init(&a);
        Node *p = parse(&a, "(foo (tr \"Hello\") [(tr \"Bye\" :id \"k\") 3])");
        const char *err = i18n_lower(&a, p, NULL, NULL);
        CHECK(err == NULL, "lowering a well-formed program with no catalogue succeeds");
        Node *foo = p->children[0];
        CHECK(foo->children[1]->type == NODE_STRING && strcmp(foo->children[1]->text, "Hello") == 0,
              "a top-level tr becomes its default string");
        Node *vec = foo->children[2];
        CHECK(vec->type == NODE_VEC && vec->children[0]->type == NODE_STRING &&
                  strcmp(vec->children[0]->text, "Bye") == 0,
              "a tr nested in a vector becomes its default string");
        CHECK(vec->children[1]->type == NODE_NUMBER, "non-tr siblings are left alone");
        arena_free_all(&a);
    }

    /* --- module on: the catalogue wins, and a missing id falls back and is counted --- */
    {
        Arena a;
        arena_init(&a);
        I18nCatalog cat;
        CHECK(load(&a, "(catalog :locale \"deDE\" [\"k1\" \"Hallo\"])", &cat), "a catalogue parses");
        CHECK(strcmp(cat.locale, "deDE") == 0 && cat.count == 1, "its locale and entry count are read");
        Node *p = parse(&a, "(f (tr \"Hello\" :id \"k1\") (tr \"Bye\" :id \"k2\"))");
        size_t fallbacks = 99;
        const char *err = i18n_lower(&a, p, &cat, &fallbacks);
        CHECK(err == NULL, "lowering with a catalogue succeeds");
        CHECK(strcmp(p->children[0]->children[1]->text, "Hallo") == 0, "a present id takes the catalogue text");
        CHECK(strcmp(p->children[0]->children[2]->text, "Bye") == 0, "an absent id keeps the default");
        CHECK(fallbacks == 1, "exactly one fallback is reported");
        arena_free_all(&a);
    }

    /* --- catalogue text round-trips through the lexer's escapes --- */
    {
        Arena a;
        arena_init(&a);
        Node *p = parse(&a, "(f (tr \"say \\\"hi\\\"\\n\\ttab\" :id \"q\"))");
        const char *text = NULL;
        CHECK(i18n_extract(&a, p, &text) == NULL, "extraction succeeds");
        I18nCatalog cat;
        CHECK(load(&a, text, &cat), "the extracted catalogue parses back");
        CHECK(cat.count == 1 && strcmp(cat.entries[0].text, "say \"hi\"\n\ttab") == 0,
              "quotes, newline and tab survive the round trip");
        CHECK(strcmp(cat.locale, "enUS") == 0, "the extracted catalogue is enUS");
        arena_free_all(&a);
    }

    /* --- extraction dedupes and refuses a conflicting id --- */
    {
        Arena a;
        arena_init(&a);
        Node *p = parse(&a, "(f (tr \"A\" :id \"x\") (tr \"A\" :id \"x\") (tr \"B\" :id \"y\"))");
        const char *text = NULL;
        CHECK(i18n_extract(&a, p, &text) == NULL, "a repeated id with the same text is fine");
        I18nCatalog cat;
        load(&a, text, &cat);
        CHECK(cat.count == 2, "two distinct ids give two entries");
        Node *bad = parse(&a, "(f (tr \"A\" :id \"x\") (tr \"B\" :id \"x\"))");
        const char *err = i18n_extract(&a, bad, &text);
        CHECK(err != NULL && strstr(err, "two different default texts") != NULL,
              "one id with two different texts is refused by name");
        arena_free_all(&a);
    }

    /* --- malformed tr forms are refused with a line number --- */
    {
        const char *bad[] = {
            "(f (tr))",
            "(f (tr 42))",
            "(f (tr \"a\" :id))",
            "(f (tr \"a\" :id 5))",
            "(f (tr \"a\" :nope \"x\"))",
            "(f (tr \"Hi {name}\"))",                       /* placeholder with no :args */
            "(f (tr \"Hi\" :args [name]))",                 /* :args with no placeholder */
            "(f (tr \"Hi {a}\" :id \"p\" :args [b]))",      /* names differ */
        };
        for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
            Arena a;
            arena_init(&a);
            Node *p = parse(&a, bad[i]);
            const char *err = i18n_lower(&a, p, NULL, NULL);
            char msg[160];
            snprintf(msg, sizeof(msg), "refused: %s", bad[i]);
            CHECK(err != NULL && strstr(err, "tr:") != NULL, msg);
            arena_free_all(&a);
        }
    }

    /* --- a string that legitimately has placeholders: extracted, but not lowerable in v0 --- */
    {
        Arena a;
        arena_init(&a);
        Node *p = parse(&a, "(f (tr \"Hi {name}\" :id \"g\" :args [name]))");
        const char *text = NULL;
        CHECK(i18n_extract(&a, p, &text) == NULL && strstr(text, "{name}") != NULL,
              "extraction keeps the placeholder text");
        const char *err = i18n_lower(&a, p, NULL, NULL);
        CHECK(err != NULL && strstr(err, "cannot be lowered yet") != NULL,
              "lowering refuses it by name instead of emitting a literal {name}");
        arena_free_all(&a);
    }

    /* --- the missing-string check --- */
    {
        Arena a;
        arena_init(&a);
        Node *p = parse(&a, "(f (tr \"Hi {n}\" :id \"g\" :args [n]) (tr \"Bye\" :id \"b\"))");
        I18nCatalog cats[I18N_LOCALE_COUNT];
        size_t n = 0;
        /* eleven complete catalogues, one per non-enUS locale */
        static char srcs[I18N_LOCALE_COUNT][160];
        for (int l = 1; l < I18N_LOCALE_COUNT; l++) {
            snprintf(srcs[l], sizeof(srcs[l]), "(catalog :locale \"%s\" [\"g\" \"X {n}\"] [\"b\" \"Y\"])",
                     I18N_LOCALES[l]);
            load(&a, srcs[l], &cats[n++]);
        }
        const char *report = NULL;
        size_t problems = 99;
        CHECK(i18n_check(&a, p, cats, n, &report, &problems) == NULL, "the check runs");
        CHECK(problems == 0 && report[0] == '\0', "eleven complete catalogues pass; enUS needs none");

        /* drop deDE's "b", break frFR's placeholder, add a stale id to esES, leave out zhTW entirely */
        static char s_de[] = "(catalog :locale \"deDE\" [\"g\" \"X {n}\"])";
        static char s_fr[] = "(catalog :locale \"frFR\" [\"g\" \"X\"] [\"b\" \"Y\"])";
        static char s_es[] = "(catalog :locale \"esES\" [\"g\" \"X {n}\"] [\"b\" \"Y\"] [\"old\" \"Z\"])";
        I18nCatalog broken[10];
        size_t m = 0;
        for (int l = 1; l < I18N_LOCALE_COUNT; l++) {
            const char *loc = I18N_LOCALES[l];
            const char *src = srcs[l];
            if (strcmp(loc, "deDE") == 0) src = s_de;
            else if (strcmp(loc, "frFR") == 0) src = s_fr;
            else if (strcmp(loc, "esES") == 0) src = s_es;
            else if (strcmp(loc, "zhTW") == 0) continue;
            load(&a, src, &broken[m++]);
        }
        CHECK(i18n_check(&a, p, broken, m, &report, &problems) == NULL, "the check runs on a broken set");
        CHECK(problems == 4, "exactly four problems: missing, placeholder, stale, absent locale");
        CHECK(strstr(report, "deDE: missing string 'b'") != NULL, "deDE's missing string is named");
        CHECK(strstr(report, "frFR: placeholders differ from the default in 'g'") != NULL,
              "frFR's placeholder mismatch is named");
        CHECK(strstr(report, "esES: stale id (no longer in the source) 'old'") != NULL, "esES's stale id is named");
        CHECK(strstr(report, "zhTW: no catalogue supplied") != NULL, "the absent locale is named");
        arena_free_all(&a);
    }

    /* --- end to end: tr -> lower -> region analysis -> the real Lua emitter --- */
    {
        Arena a;
        arena_init(&a);
        const char *src = "(defn greeting [] : String (tr \"Hello\" :id \"greet\"))";
        I18nCatalog cat;
        load(&a, "(catalog :locale \"deDE\" [\"greet\" \"Hallo\"])", &cat);
        Node *p = parse(&a, src);
        const char *err = i18n_lower(&a, p, &cat, NULL);
        const char *lua = NULL;
        if (!err) {
            err = region_analyze(&a, p);
            if (!err) lua = emit_lua(&a, p, &err);
        }
        if (err) printf("  (pipeline error: %s)\n", err);
        CHECK(lua != NULL, "a program using tr builds through the Lua emitter");
        CHECK(lua && strstr(lua, "Hallo") != NULL, "the emitted Lua carries the German string");
        CHECK(lua && strstr(lua, "Hello") == NULL, "and not the English default");
        arena_free_all(&a);

        arena_init(&a);
        Node *p2 = parse(&a, src);
        err = i18n_lower(&a, p2, NULL, NULL);
        const char *lua2 = NULL;
        if (!err) {
            err = region_analyze(&a, p2);
            if (!err) lua2 = emit_lua(&a, p2, &err);
        }
        CHECK(lua2 && strstr(lua2, "Hello") != NULL, "with the module off the emitted Lua carries the default");
        arena_free_all(&a);
    }

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
