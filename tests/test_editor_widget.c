/* tests/test_editor_widget.c -- real end-to-end verification of
 * stdlib/editor/widget.prn's Toggle type, the first real slice of the
 * "UI widget system" founder real-time asked for while testing the
 * status-bar auto-indent control ("start building out a ui widget
 * system or something for reusable ui elements", chosen as the next
 * thread after v0.77.0-v0.80.0 shipped).
 *
 * Two real levels, same discipline every other test in this repo
 * already uses: (1) toggle-hit?/toggle-on?/toggle-flip/toggle-label are
 * pure functions, checked directly against real coordinates and real
 * state transitions; (2) render-toggle is checked for real, error-free
 * integration against the real SDL2 renderer/font under a real Xvfb
 * display -- the exact same set-draw-color+render-fill-rect+render-text
 * call sequence the hand-rolled status-bar block in examples/
 * editor_main.c already used and had screenshot-verified in the
 * v0.80.0 zoom work, now behind the shared widget instead of inline.
 */
#include "parena_runtime.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "test_editor_widget_gen.c"

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s\n", msg); failures++; } \
    else { printf("PASS: %s\n", msg); } \
} while (0)

int main(void) {
    Arena a;
    arena_init(&a);

    /* --- real construction + hit-testing (pure, no SDL needed) --- */
    Toggle t = new_toggle(10, 20, 100, 30, "ON label", "OFF label", 1);
    CHECK(t.x == 10 && t.y == 20 && t.w == 100 && t.h == 30, "new-toggle stores the real bounds it was given");
    CHECK(toggle_on_(&t), "new-toggle with initial-on=true starts on");
    CHECK(strcmp(toggle_label(&t), "ON label") == 0, "toggle-label reflects the real on-state label");

    CHECK(toggle_hit_(&t, 50, 30), "toggle-hit? is true for a point well inside the real rect");
    CHECK(toggle_hit_(&t, 10, 20), "toggle-hit? is true at the real top-left corner (inclusive)");
    CHECK(!toggle_hit_(&t, 110, 30), "toggle-hit? is false at the real right edge (exclusive)");
    CHECK(!toggle_hit_(&t, 50, 50), "toggle-hit? is false at the real bottom edge (exclusive)");
    CHECK(!toggle_hit_(&t, 9, 30), "toggle-hit? is false one pixel left of the real rect");
    CHECK(!toggle_hit_(&t, 50, 19), "toggle-hit? is false one pixel above the real rect");

    /* --- real functional-update flip (no struct mutation -- returns a
     * NEW Toggle value, same shape Buffer's own insert/set-cursor use) --- */
    Toggle flipped = toggle_flip(&t, &a);
    CHECK(!toggle_on_(&flipped), "toggle-flip turns a real on toggle off");
    CHECK(strcmp(toggle_label(&flipped), "OFF label") == 0, "toggle-label reflects the real flipped-off label");
    CHECK(toggle_on_(&t), "the original Toggle value is untouched -- toggle-flip is a real functional update, not a mutation");
    Toggle flipped_back = toggle_flip(&flipped, &a);
    CHECK(toggle_on_(&flipped_back), "flipping twice returns to the real original on-state");
    CHECK(flipped_back.x == t.x && flipped_back.w == t.w, "toggle-flip preserves the real bounds across the flip");

    /* --- real end-to-end render: an actual Toggle drawn through the
     * real SDL2 stack, matching examples/editor_main.c's own real
     * status-bar usage --- */
    Result r = init(&a);
    CHECK(r.tag == 1, "sdl2/init succeeds under a real X display");
    if (r.tag != 1) { arena_free_all(&a); return failures ? 1 : 0; }

    Result wr = create_window("PARENA editor v0 -- widget test", 320, 200, &a);
    CHECK(wr.tag == 1, "sdl2/create-window opens a real window");
    if (wr.tag != 1) { arena_free_all(&a); return 1; }
    Window win = *(Window *)wr.value;

    Result rr = create_renderer(&win, &a);
    CHECK(rr.tag == 1, "sdl2/create-renderer succeeds");
    if (rr.tag != 1) { destroy_window(win); arena_free_all(&a); return 1; }
    Renderer ren = *(Renderer *)rr.value;

    Result ttfr = ttf_init(&a);
    CHECK(ttfr.tag == 1, "sdl2/ttf-init succeeds");
    Result fontr = open_font("/usr/share/fonts/truetype/jetbrains-mono/JetBrainsMono-Regular.ttf", 16, &a);
    CHECK(fontr.tag == 1, "sdl2/open-font loads the real font");
    if (fontr.tag != 1) { destroy_renderer(ren); destroy_window(win); arena_free_all(&a); return 1; }
    Font font = *(Font *)fontr.value;

    Result cbg = set_draw_color(&ren, 30, 30, 35, 255, &a);
    Result clr = render_clear(&ren, &a);
    CHECK(cbg.tag == 1 && clr.tag == 1, "background clear succeeds before drawing the widget");

    Result togr = render_toggle(&ren, &font, &t, 45, 45, 52, 200, 200, 200, &a);
    CHECK(togr.tag == 1, "render-toggle draws the real widget (bg rect + label) with no error");
    render_present(&ren);
    delay(16);

    printf("\n%d failure(s)\n", failures);
    arena_free_all(&a);
    return failures ? 1 : 0;
}
