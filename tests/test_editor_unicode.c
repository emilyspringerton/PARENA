/* tests/test_editor_unicode.c -- real end-to-end verification of the UTF-8 codepoint-boundary
 * fix in stdlib/editor/buffer.prn (kanban cruise-queue card 3454353, "fix unicode in parena
 * editor"). Real, genuine bug this pins down: every cursor-movement/delete function used to move
 * exactly one BYTE, not one UTF-8 codepoint -- for any real multi-byte character, a single
 * Backspace/Left/Right/Delete only touched one byte of a real, multi-byte sequence, corrupting
 * it. Deliberately exercises both a real 2-byte codepoint (e/acute, U+00E9, 0xC3 0xA9) and a
 * real 4-byte codepoint (a grinning-face emoji, U+1F600, 0xF0 0x9F 0x98 0x80) -- not just ASCII,
 * which would pass even with the old, broken byte-at-a-time code.
 *
 * No SDL needed -- calls the real, compiled buffer.prn functions directly, same "test what's
 * actually there" discipline tests/test_sip_message.c/tests/test_wire.c already established.
 */
#include "parena_runtime.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>

#include "test_editor_unicode_gen.c"

int main(void) {
    Arena arena;
    arena_init(&arena);

    /* "caf" + e/acute: c a f [0xC3 0xA9] -- 5 real bytes, 4 real codepoints. */
    const char *cafe = "caf\xc3\xa9";
    Buffer buf = from_text((char *)cafe);
    assert(cursor_pos(&buf) == 5); /* cursor starts at the real end -- byte length, not codepoint count */

    /* Left once: must land at byte 3 (the START of the 2-byte e/acute), not byte 4 (its own
     * second, continuation byte -- the real, old, broken behavior). */
    Buffer buf2 = move_cursor_left(&buf);
    assert(cursor_pos(&buf2) == 3);

    /* Right once from byte 3: must land back at byte 5 (skipping the WHOLE 2-byte codepoint),
     * not byte 4. */
    Buffer buf3 = move_cursor_right(&buf2);
    assert(cursor_pos(&buf3) == 5);

    /* Backspace at the real end (byte 5): must remove the ENTIRE 2-byte e/acute, leaving real
     * "caf" (3 bytes), not a truncated, invalid single trailing byte. */
    Result br = backspace_at_cursor(&buf3, &arena);
    assert(br.tag == 1);
    Buffer *buf4 = (Buffer *)br.value;
    assert(strcmp(active_text(buf4), "caf") == 0);
    assert(cursor_pos(buf4) == 3);

    /* "a" + grinning-face emoji (4 real bytes) + "b" -- 6 real bytes, 3 real codepoints. */
    const char *emoji = "a\xf0\x9f\x98\x80""b";
    Buffer ebuf = from_text((char *)emoji);
    assert(cursor_pos(&ebuf) == 6);

    /* Left once from byte 6: lands at byte 5 (the real, 1-byte start of "b"). */
    Buffer ebuf2 = move_cursor_left(&ebuf);
    assert(cursor_pos(&ebuf2) == 5);

    /* Left again from byte 5: must walk back across all 3 real continuation bytes to byte 1
     * (the START of the 4-byte emoji), not byte 4/3/2 (a real, live, corrupting mid-codepoint
     * landing spot the old byte-at-a-time code would have produced). */
    Buffer ebuf3 = move_cursor_left(&ebuf2);
    assert(cursor_pos(&ebuf3) == 1);

    /* Delete-forward with the cursor right before the emoji: must remove the ENTIRE 4-byte
     * codepoint, leaving real "ab" (2 bytes). */
    Result dr = delete_forward_at_cursor(&ebuf3, &arena);
    assert(dr.tag == 1);
    Buffer *ebuf4 = (Buffer *)dr.value;
    assert(strcmp(active_text(ebuf4), "ab") == 0);
    assert(cursor_pos(ebuf4) == 1);

    printf("test_editor_unicode: all real UTF-8 codepoint-boundary assertions passed\n");
    return 0;
}
