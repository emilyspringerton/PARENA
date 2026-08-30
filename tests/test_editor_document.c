/* tests/test_editor_document.c -- real end-to-end verification of stdlib/editor/document.prn
 * (founder real-time: "we need document management asap"). Ties editor/buffer.prn (text/cursor)
 * and papercraft/note_version_mod.prn (coalesce/version decisions, S215-02) together.
 */
#include "parena_runtime.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>

#include "test_editor_document_gen.c"

int main(void) {
    Arena arena;
    arena_init(&arena);

    Document doc = new_document(1000, &arena);
    assert(document_version(&doc) == 1);
    assert(strcmp(document_text(&doc), "") == 0);

    /* First real edit, 5s later -- within the 30s coalesce window, version stays 1. */
    doc = apply_edit(doc, "Hello", 1005, &arena);
    assert(document_version(&doc) == 1);
    assert(strcmp(document_text(&doc), "Hello") == 0);

    /* Second edit, 10s after that (still within the coalesce window) -- coalesces in place. */
    doc = apply_edit(doc, "Hello world", 1015, &arena);
    assert(document_version(&doc) == 1);
    assert(strcmp(document_text(&doc), "Hello world") == 0);

    /* Real gap: 60s later, past the coalesce window -- forks a real new version. */
    doc = apply_edit(doc, "Hello world, revised", 1075, &arena);
    assert(document_version(&doc) == 2);
    assert(strcmp(document_text(&doc), "Hello world, revised") == 0);

    printf("test_editor_document: all assertions passed\n");
    return 0;
}
