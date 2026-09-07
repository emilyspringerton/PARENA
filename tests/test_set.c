/* tests/test_set.c -- real end-to-end verification of stdlib/set.prn (PARENA cybersecurity/
 * data-analysis-primitives thread, 2026-09-07 follow-up: founder's own pasted "Set Theory and
 * Set Convergence Primitives (set) ... intersections, unions, and set differences" proposal,
 * named against the real use case: intersecting known-malicious IOCs against live connections).
 */
#include "parena_runtime.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "test_set_gen.c"

static int set_has(StringSet *s, const char *key) {
    return set_contains_(s, (char *)key);
}

int main(void) {
    Arena arena;
    arena_init(&arena);

    /* --- basic add/contains/size --- */
    {
        StringSet s = set_new(16, &arena);
        assert(set_size(&s) == 0);

        Result r1 = set_add_(&s, (char *)"evil.example.com", &arena);
        assert(r1.tag == 1);
        assert(set_size(&s) == 1);
        assert(set_has(&s, "evil.example.com"));
        assert(!set_has(&s, "benign.example.com"));

        /* real, standard set semantics: re-adding an existing member is a no-op, not growth */
        Result r2 = set_add_(&s, (char *)"evil.example.com", &arena);
        assert(r2.tag == 1);
        assert(set_size(&s) == 1);

        Result r3 = set_add_(&s, (char *)"c2.example.net", &arena);
        assert(r3.tag == 1);
        assert(set_size(&s) == 2);
        printf("PASS: real add/contains/size (including no-op re-add) all correct\n");
    }

    /* --- a real, honest Full boundary: a deliberately tiny, saturated table --- */
    {
        StringSet tiny = set_new(1, &arena);
        Result r1 = set_add_(&tiny, (char *)"first", &arena);
        assert(r1.tag == 1);
        Result r2 = set_add_(&tiny, (char *)"second", &arena);
        assert(r2.tag == 0); /* Err(Full) -- the one real slot is already taken */
        printf("PASS: a real, saturated 1-slot table honestly reports Full on a second, "
               "distinct key, rather than silently overwriting or growing\n");
    }

    /* --- set-to-vec: real materialization of every real member --- */
    {
        StringSet s = set_new(16, &arena);
        set_add_(&s, (char *)"a.com", &arena);
        set_add_(&s, (char *)"b.com", &arena);
        set_add_(&s, (char *)"c.com", &arena);
        Vec v = set_to_vec(&s, &arena);
        assert(vec_len(&v) == 3);
        int found_a = 0, found_b = 0, found_c = 0;
        for (int i = 0; i < vec_len(&v); i++) {
            char *m = (char *)vec_get(&v, i);
            if (strcmp(m, "a.com") == 0) found_a = 1;
            if (strcmp(m, "b.com") == 0) found_b = 1;
            if (strcmp(m, "c.com") == 0) found_c = 1;
        }
        assert(found_a && found_b && found_c);
        printf("PASS: set-to-vec materializes every real member exactly once\n");
    }

    /* --- the real headline: intersect/union/difference, matching the founder's own real IOC
     * use case (malicious domains vs a set of observed connections) --- */
    {
        StringSet malicious = set_new(16, &arena);
        set_add_(&malicious, (char *)"evil.example.com", &arena);
        set_add_(&malicious, (char *)"c2.example.net", &arena);
        set_add_(&malicious, (char *)"phish.example.org", &arena);

        StringSet observed = set_new(16, &arena);
        set_add_(&observed, (char *)"evil.example.com", &arena);   /* real overlap */
        set_add_(&observed, (char *)"c2.example.net", &arena);     /* real overlap */
        set_add_(&observed, (char *)"google.com", &arena);         /* benign, no overlap */
        set_add_(&observed, (char *)"github.com", &arena);         /* benign, no overlap */

        StringSet hits = set_intersect(&malicious, &observed, &arena);
        assert(set_size(&hits) == 2);
        assert(set_has(&hits, "evil.example.com"));
        assert(set_has(&hits, "c2.example.net"));
        assert(!set_has(&hits, "phish.example.org"));
        printf("PASS: real ∩ (set-intersect) finds exactly the 2 real IOC hits against "
               "observed connections\n");

        StringSet everything = set_union(&malicious, &observed, &arena);
        assert(set_size(&everything) == 5); /* 3 + 4 - 2 real overlaps */
        assert(set_has(&everything, "phish.example.org"));
        assert(set_has(&everything, "google.com"));
        printf("PASS: real ∪ (set-union) produces the correct, real deduplicated union (5, "
               "not 7)\n");

        StringSet missed = set_difference(&malicious, &observed, &arena);
        assert(set_size(&missed) == 1);
        assert(set_has(&missed, "phish.example.org"));
        assert(!set_has(&missed, "evil.example.com"));
        printf("PASS: real \\ (set-difference) correctly finds the 1 real IOC never actually "
               "observed\n");
    }

    printf("test_set: all real assertions passed\n");
    return 0;
}
