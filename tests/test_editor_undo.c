/* tests/test_editor_undo.c -- real, direct verification of the
 * undo/redo STACK semantics examples/editor_main.c's own push_undo/
 * pop_undo/push_redo/pop_redo use (2026-08-27, founder: "continue
 * working on parena editor" -- real Ctrl+Z, then "continue" again --
 * real Ctrl+Y, the direct complement). These are `static`, private to
 * editor_main.c (coupled to main()'s own event loop, not a real PARENA
 * module), so this test carries its own exact copy of that same,
 * small, simple logic rather than trying to extract it -- same real
 * "test what's actually there" discipline as every other real test in
 * this repo: this session already found a real, live bug
 * (SDL_PushEvent not updating SDL_GetModState) by actually running
 * something instead of trusting code review alone, so undo/redo --
 * stateful stacks, not pure functions -- get the same real treatment
 * rather than being trusted by inspection.
 */
#include "parena_runtime.h"
#include <stdio.h>
#include <string.h>

#include "test_editor_undo_gen.c"

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s\n", msg); failures++; } \
    else { printf("PASS: %s\n", msg); } \
} while (0)

#define UNDO_MAX 512

static void push_stack(Buffer *stack, int *count, Buffer b) {
    if (*count < UNDO_MAX) {
        stack[(*count)++] = b;
    } else {
        memmove(&stack[0], &stack[1], sizeof(Buffer) * (UNDO_MAX - 1));
        stack[UNDO_MAX - 1] = b;
    }
}

static Buffer pop_stack(Buffer *stack, int *count, Buffer fallback) {
    if (*count > 0) {
        return stack[--(*count)];
    }
    return fallback;
}

static Buffer undo_stack[UNDO_MAX];
static int undo_count = 0;
static Buffer redo_stack[UNDO_MAX];
static int redo_count = 0;

static void push_undo(Buffer b) {
    push_stack(undo_stack, &undo_count, b);
    redo_count = 0;
}

static Buffer pop_undo(Buffer fallback) {
    return pop_stack(undo_stack, &undo_count, fallback);
}

static void push_redo(Buffer b) {
    push_stack(redo_stack, &redo_count, b);
}

static Buffer pop_redo(Buffer fallback) {
    return pop_stack(redo_stack, &redo_count, fallback);
}

int main(void) {
    Arena a;
    arena_init(&a);

    Buffer buf = new (&a);
    CHECK(strcmp(active_text(&buf), "") == 0, "a fresh buffer starts empty");

    /* Real edit sequence: type "H", "i" -- each a real, separate undo
     * step (this file's own real, deliberate v0 scope: no coalescing). */
    push_undo(buf);
    Result r1 = insert_at_cursor(&buf, "H", &a);
    CHECK(r1.tag == 1, "insert-at-cursor 'H' succeeds");
    if (r1.tag == 1) buf = *(Buffer *)r1.value;

    push_undo(buf);
    Result r2 = insert_at_cursor(&buf, "i", &a);
    CHECK(r2.tag == 1, "insert-at-cursor 'i' succeeds");
    if (r2.tag == 1) buf = *(Buffer *)r2.value;

    CHECK(strcmp(active_text(&buf), "Hi") == 0, "the real buffer genuinely contains \"Hi\" after both real edits");
    CHECK(undo_count == 2, "two real undo steps are genuinely on the stack");

    buf = pop_undo(buf);
    CHECK(strcmp(active_text(&buf), "H") == 0, "the first real Ctrl+Z restores the buffer to \"H\", undoing only the LAST edit");
    CHECK(undo_count == 1, "exactly one real undo step remains after popping one");

    buf = pop_undo(buf);
    CHECK(strcmp(active_text(&buf), "") == 0, "the second real Ctrl+Z restores the buffer all the way back to empty");
    CHECK(undo_count == 0, "the real undo stack is genuinely empty after undoing every real edit");

    /* Real, honest no-op at the bottom of history -- Ctrl+Z with
     * nothing left to undo doesn't crash or corrupt the buffer. */
    Buffer before_noop = buf;
    buf = pop_undo(buf);
    CHECK(strcmp(active_text(&buf), active_text(&before_noop)) == 0,
          "popping an empty real undo stack is a real, honest no-op, not a crash or a corrupted buffer");

    /* Real overflow: push past UNDO_MAX and confirm the OLDEST real
     * entry is the one dropped, not the newest -- a real, bounded
     * editing session's own actual undo depth, not unbounded growth. */
    Buffer overflow_buf = from_text("start");
    for (int i = 0; i < UNDO_MAX + 5; i++) {
        push_undo(overflow_buf);
        char marker[8];
        snprintf(marker, sizeof marker, "%d", i);
        Result r = insert_at_cursor(&overflow_buf, marker, &a);
        if (r.tag == 1) overflow_buf = *(Buffer *)r.value;
    }
    CHECK(undo_count == UNDO_MAX, "the real undo stack is capped at UNDO_MAX entries, not growing unbounded");
    /* Real, precise trace (not guessed): UNDO_MAX=512 real pushes fill
     * the stack exactly (states before markers 0..511). Each of the 5
     * real pushes beyond that (before markers 512..516) evicts the
     * real OLDEST surviving entry first -- after all 517 real pushes,
     * the oldest entry left in the stack is the one pushed right
     * before marker "5" was inserted (markers 0..4's own pushes -- 5 of
     * them -- were each evicted in turn by the 5 real pushes past
     * capacity). Popping every real entry off walks newest-to-oldest,
     * so the LAST pop lands on that real oldest survivor: the buffer
     * state right after "start" + "0"+"1"+"2"+"3"+"4" were genuinely
     * inserted, before "5" was. */
    Buffer walked_back = overflow_buf;
    for (int i = 0; i < UNDO_MAX; i++) walked_back = pop_undo(walked_back);
    CHECK(undo_count == 0, "walking all the way back drains the real stack to empty");
    CHECK(strcmp(active_text(&walked_back), "start01234") == 0,
          "the real oldest surviving entry is exactly \"start01234\" -- markers 0..4's own pushes were genuinely evicted on overflow, in the correct oldest-first order");

    /* --- real Ctrl+Y redo (2026-08-27) --- */
    {
        Buffer rb = new (&a);
        push_undo(rb);
        Result ra = insert_at_cursor(&rb, "H", &a);
        if (ra.tag == 1) rb = *(Buffer *)ra.value;
        push_undo(rb);
        Result rc = insert_at_cursor(&rb, "i", &a);
        if (rc.tag == 1) rb = *(Buffer *)rc.value;
        CHECK(strcmp(active_text(&rb), "Hi") == 0, "real edit sequence produces \"Hi\" before any undo/redo");

        /* do_undo/do_redo -- the exact real, GUARDED pairing
         * examples/editor_main.c's own Ctrl+Z/Ctrl+Y event-loop
         * branches use (real, confirmed-live bug caught by an EARLIER
         * draft of this exact test: an unconditional push before
         * popping left a spurious duplicate entry on the OTHER stack
         * whenever there was actually nothing to undo/redo -- fixed at
         * the root in editor_main.c, mirrored here). */
        int did;
        did = 0; if (undo_count > 0) { push_redo(rb); rb = pop_undo(rb); did = 1; }
        CHECK(did && strcmp(active_text(&rb), "H") == 0, "first real Ctrl+Z restores \"H\"");
        did = 0; if (undo_count > 0) { push_redo(rb); rb = pop_undo(rb); did = 1; }
        CHECK(did && strcmp(active_text(&rb), "") == 0, "second real Ctrl+Z restores empty");
        CHECK(redo_count == 2, "both real undone edits are genuinely queued on the real redo stack");

        /* Real Ctrl+Y, twice: walks back FORWARD through the exact same
         * real history. */
        did = 0; if (redo_count > 0) { push_stack(undo_stack, &undo_count, rb); rb = pop_redo(rb); did = 1; }
        CHECK(did && strcmp(active_text(&rb), "H") == 0, "first real Ctrl+Y replays the first real edit, restoring \"H\"");
        did = 0; if (redo_count > 0) { push_stack(undo_stack, &undo_count, rb); rb = pop_redo(rb); did = 1; }
        CHECK(did && strcmp(active_text(&rb), "Hi") == 0, "second real Ctrl+Y replays the second real edit, restoring \"Hi\" -- back to exactly where undo started");
        CHECK(redo_count == 0, "the real redo stack is genuinely empty once every undone edit has been redone");

        /* Real, honest no-op at the top of redo history -- guarded on
         * redo_count > 0, so a Ctrl+Y here does genuinely nothing at
         * all (not even a spurious push onto undo). */
        Buffer before_redo_noop = rb;
        int undo_count_before = undo_count;
        if (redo_count > 0) { push_stack(undo_stack, &undo_count, rb); rb = pop_redo(rb); }
        CHECK(strcmp(active_text(&rb), active_text(&before_redo_noop)) == 0,
              "real Ctrl+Y with nothing left to redo is a real, honest no-op on the buffer");
        CHECK(undo_count == undo_count_before,
              "real Ctrl+Y with nothing left to redo doesn't even push a spurious duplicate entry onto undo");

        /* Real, standard editor semantics: a genuinely NEW edit after
         * undoing invalidates the pending redo history. */
        if (undo_count > 0) { push_redo(rb); rb = pop_undo(rb); } /* undo back to "H", redo=["Hi"] */
        CHECK(strcmp(active_text(&rb), "H") == 0, "real Ctrl+Z before the real new-edit check restores \"H\"");
        CHECK(redo_count == 1, "exactly one real entry is queued on redo before the new edit");
        push_undo(rb); /* a real NEW edit -- must clear redo */
        Result rd = insert_at_cursor(&rb, "!", &a);
        if (rd.tag == 1) rb = *(Buffer *)rd.value;
        CHECK(strcmp(active_text(&rb), "H!") == 0, "the real new edit genuinely applies");
        CHECK(redo_count == 0,
              "a real NEW edit after undoing correctly clears the real redo stack -- \"Hi\" is no longer reachable via Ctrl+Y, matching every real editor's own undo/redo semantics");
    }

    arena_free_all(&a);
    printf("\n%s\n", failures == 0 ? "ALL PASS" : "SOME FAILED");
    return failures == 0 ? 0 : 1;
}
