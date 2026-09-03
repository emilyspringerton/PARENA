/* tests/test_bstree.c -- real end-to-end verification of stdlib/bstree.prn (kanban priority-
 * queue card "9933: INDEXING primitives built into PARENA to power IDUNA OG unified search -
 * btries etc"). Confirms real insert/get/contains? behavior, insert-or-update semantics,
 * multi-key ordering (both sides of the tree exercised, not just a trivial one-key case), and a
 * real miss on an absent key -- not just "did it compile".
 */
#include "parena_runtime.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>

#include "test_bstree_gen.c"

int main(void) {
    Arena arena;
    arena_init(&arena);

    BSTree t = new(&arena);

    /* Empty tree: a real miss, not a crash. */
    Option miss = get(&t, "anything", &arena);
    assert(miss.tag == 0);
    assert(contains_(&t, "anything", &arena) == 0);

    /* Real inserts spanning both sides of the tree (lexicographic: "m" < "z", "m" > "a"). */
    insert_(&t, "m", 100, &arena);
    insert_(&t, "a", 1, &arena);
    insert_(&t, "z", 26, &arena);
    insert_(&t, "b", 2, &arena);
    insert_(&t, "y", 25, &arena);

    /* Real lookups, each real value round-tripped correctly. */
    Option r;
    r = get(&t, "m", &arena);
    assert(r.tag == 1 && *(int *)r.value == 100);
    r = get(&t, "a", &arena);
    assert(r.tag == 1 && *(int *)r.value == 1);
    r = get(&t, "z", &arena);
    assert(r.tag == 1 && *(int *)r.value == 26);
    r = get(&t, "b", &arena);
    assert(r.tag == 1 && *(int *)r.value == 2);
    r = get(&t, "y", &arena);
    assert(r.tag == 1 && *(int *)r.value == 25);

    /* A real miss on a key that was never inserted, even though it would sort between two real
     * keys ("c" between "b" and "m") -- not a false positive from tree-structure proximity. */
    Option missing = get(&t, "c", &arena);
    assert(missing.tag == 0);
    assert(contains_(&t, "c", &arena) == 0);
    assert(contains_(&t, "m", &arena) == 1);

    /* Real insert-or-update: re-inserting an existing key updates its value in place rather
     * than creating a duplicate node or being silently ignored. */
    insert_(&t, "m", 999, &arena);
    r = get(&t, "m", &arena);
    assert(r.tag == 1 && *(int *)r.value == 999);
    /* Node count unchanged -- confirms this was a real update, not a new node appended. */
    assert(vec_len(&t.nodes) == 5);

    printf("test_bstree: all assertions passed\n");
    return 0;
}
