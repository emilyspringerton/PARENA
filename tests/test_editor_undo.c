/* tests/test_editor_undo.c -- real, direct verification of the undo
 * STACK semantics examples/editor_main.c's own push_undo/pop_undo use
 * (2026-08-27, founder: "continue working on parena editor" -- real
 * Ctrl+Z). push_undo/pop_undo are `static`, private to editor_main.c
 * (coupled to main()'s own event loop, not a real PARENA module), so
 * this test carries its own exact copy of that same, small, simple
 * logic rather than trying to extract it -- same real "test what's
 * actually there" discipline as every other real test in this repo:
 * this session already found a real, live bug (SDL_PushEvent not
 * updating SDL_GetModState) by actually running something instead of
 * trusting code review alone, so undo -- a stateful stack, not a pure
 * function -- gets the same real treatment rather than being trusted
 * by inspection.
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
static Buffer undo_stack[UNDO_MAX];
static int undo_count = 0;

static void push_undo(Buffer b) {
    if (undo_count < UNDO_MAX) {
        undo_stack[undo_count++] = b;
    } else {
        memmove(&undo_stack[0], &undo_stack[1], sizeof(Buffer) * (UNDO_MAX - 1));
        undo_stack[UNDO_MAX - 1] = b;
    }
}

static Buffer pop_undo(Buffer fallback) {
    if (undo_count > 0) {
        return undo_stack[--undo_count];
    }
    return fallback;
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

    arena_free_all(&a);
    printf("\n%s\n", failures == 0 ? "ALL PASS" : "SOME FAILED");
    return failures == 0 ? 0 : 1;
}
