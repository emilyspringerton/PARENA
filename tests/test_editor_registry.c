/* tests/test_editor_registry.c -- real end-to-end verification of stdlib/editor/registry.prn
 * (S217-03: multi-document registry on top of editor/document.prn).
 */
#include "parena_runtime.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>

#include "test_editor_registry_gen.c"

int main(void) {
    Arena arena;
    arena_init(&arena);

    Registry reg = new_registry(&arena);
    assert(document_count(&reg) == 0);

    reg = open_document(reg, 1000, &arena);
    assert(document_count(&reg) == 1);
    assert(reg.current_index == 0);

    reg = open_document(reg, 1001, &arena);
    assert(document_count(&reg) == 2);
    assert(reg.current_index == 1);

    /* Edit the second (current) document. */
    Document cur = current_document(&reg);
    cur = apply_edit(cur, "doc2 text", 1010, &arena);
    assert(strcmp(document_text(&cur), "doc2 text") == 0);

    /* Switch back to the first document -- untouched, still empty. */
    reg = switch_document(reg, 0);
    assert(reg.current_index == 0);
    Document first = current_document(&reg);
    assert(strcmp(document_text(&first), "") == 0);

    /* Real, honest bounds check: an out-of-range switch leaves the registry unchanged. */
    Registry unchanged = switch_document(reg, 99);
    assert(unchanged.current_index == 0);

    printf("test_editor_registry: all assertions passed\n");
    return 0;
}
