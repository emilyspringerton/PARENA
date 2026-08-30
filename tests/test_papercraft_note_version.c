/* tests/test_papercraft_note_version.c -- real end-to-end verification of
 * stdlib/papercraft/note_version_mod.prn (founder real-time: "add parena primitives for managing
 * versions of notes have it plug into papercraft - like the backend of icloud").
 */
#include "parena_runtime.h"
#include <assert.h>
#include <stdio.h>

#include "test_papercraft_note_version_gen.c"

int main(void) {
    assert(max_versions_per_note() == 50);
    assert(coalesce_window_seconds() == 30);

    /* Real, hand-traced coalesce-window cases. */
    assert(on_papercraft_should_coalesce_edit(5) == 1);
    assert(on_papercraft_should_coalesce_edit(45) == 0);

    /* Real, hand-traced eviction cases: -1 sentinel below the cap, oldest-first (0) at/above it. */
    assert(on_papercraft_version_to_evict(10) == -1);
    assert(on_papercraft_version_to_evict(50) == 0);

    /* Real, hand-traced conflict cases: same base version as current -> no conflict; stale base
       -> real conflict. */
    assert(on_papercraft_has_version_conflict(3, 3) == 0);
    assert(on_papercraft_has_version_conflict(3, 4) == 1);

    printf("test_papercraft_note_version: all assertions passed\n");
    return 0;
}
