/* tests/test_ui_menu.c -- stdlib/ui/menu.prn (card #483).
 *
 * Two layers: hand-derived checks, and a differential check against a VERBATIM copy of SHANKPIT's own
 * C implementation (apps/lobby/src/main.c lobby_nav_move / lobby_button_pos / lobby_hit_test, the
 * SDL_MOUSEBUTTONDOWN double-click thresholds) over every grid size/selection/direction and a dense
 * pointer sweep -- the PARENA port must agree with the code it replaces everywhere, not just on the
 * cases someone thought of. */
#include "parena_runtime.h"
#include <stdio.h>
#include <math.h>

#include "test_ui_menu_gen.c"

static int failures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { printf("FAIL: %s\n", msg); failures++; } \
    else { printf("PASS: %s\n", msg); } } while (0)

/* ---- verbatim from SHANKPIT apps/lobby/src/main.c (cols fixed at 2 there) ---- */
typedef struct { float menu_x, menu_y, column_w, row_gap, icon_size; } Lay;
static int ref_cols(int n) { (void)n; return 2; }
static void ref_pos(int index, int n, const Lay *l, float *x, float *y) {
    int cols = ref_cols(n); int col = index % cols; int row = index / cols;
    *x = l->menu_x + l->column_w * (float)col;
    *y = l->menu_y - l->row_gap * (float)row;
}
static int ref_nav(int selection, int menu_count, int dx, int dy) {
    if (menu_count <= 0) return 0;
    int cols = ref_cols(menu_count);
    int rows = (menu_count + cols - 1) / cols;
    int col = selection % cols; int row = selection / cols;
    if (dx != 0) {
        int next_col = (col + dx + cols) % cols;
        int idx = row * cols + next_col;
        if (idx >= menu_count) idx = row * cols;
        if (idx >= menu_count) idx = menu_count - 1;
        return idx;
    }
    if (dy != 0) {
        int next_row = (row + dy + rows) % rows;
        int idx = next_row * cols + col;
        if (idx >= menu_count) idx = menu_count - 1;
        return idx;
    }
    return selection;
}
static int ref_hit(float mx, float my, int n, const Lay *l) {
    for (int i = 0; i < n; i++) {
        float x, y; ref_pos(i, n, l, &x, &y);
        if (mx >= x && mx <= x + l->icon_size && my >= y && my <= y + l->icon_size) return i;
    }
    return -1;
}
static int ref_click(int last_idx, unsigned last_ms, int hit, unsigned now) {
    if (last_idx == hit && last_ms > 0) {
        unsigned delta = now - last_ms;
        if (delta <= 250) return 1;
        if (delta <= 700) return 2;
    }
    return 0;
}

int main(void) {
    /* hand-derived: a 10-cell, 2-column grid */
    CHECK(menu_col(7, 2) == 1 && menu_row(7, 2) == 3, "cell 7 of 2 columns is col 1, row 3");
    CHECK(menu_rows(10, 2) == 5 && menu_rows(9, 2) == 5 && menu_rows(1, 2) == 1, "row count is ceil(count/cols)");
    CHECK(fabs(menu_cell_x(3, 2, 360.0, 640.0) - 1000.0) < 1e-9, "cell 3 x = 360 + 640*1");
    CHECK(fabs(menu_cell_y(3, 2, 450.0, 105.0) - 345.0) < 1e-9, "cell 3 y = 450 - 105*1");
    CHECK(menu_nav_move(0, 10, 2, 1, 0) == 1, "right from cell 0 -> 1");
    CHECK(menu_nav_move(1, 10, 2, 1, 0) == 0, "right from the last column wraps to column 0");
    CHECK(menu_nav_move(8, 10, 2, 0, 1) == 0, "down from the bottom row wraps to the top");
    CHECK(menu_nav_move(0, 10, 2, 0, -1) == 8, "up from the top wraps to the bottom");
    CHECK(menu_nav_move(8, 9, 2, 1, 0) == 8, "9 cells: right from 8 (missing cell 9) snaps to the row start 8");
    CHECK(menu_nav_move(7, 9, 2, 0, 1) == 8, "9 cells: down from 7 into the missing cell snaps to the last cell");
    CHECK(menu_nav_move(5, 10, 2, 0, 0) == 5, "no move keeps the selection");
    CHECK(menu_nav_move(0, 0, 2, 1, 0) == 0, "an empty menu returns 0");
    CHECK(menu_hit_test(360.0, 450.0, 10, 2, 360.0, 450.0, 640.0, 105.0, 88.0) == 0, "pointer on cell 0's corner hits it (inclusive)");
    CHECK(menu_hit_test(448.0, 538.0, 10, 2, 360.0, 450.0, 640.0, 105.0, 88.0) == 0, "pointer on cell 0's far corner hits it (inclusive)");
    CHECK(menu_hit_test(449.0, 450.0, 10, 2, 360.0, 450.0, 640.0, 105.0, 88.0) == -1, "pointer just right of cell 0 misses");
    CHECK(menu_hit_test(1000.0, 345.0, 10, 2, 360.0, 450.0, 640.0, 105.0, 88.0) == 3, "pointer on cell 3 hits 3");
    CHECK(menu_click_kind(-1, 0, 4, 1000) == 0, "no previous click: remember");
    CHECK(menu_click_kind(4, 1000, 4, 1250) == 1, "250 ms on the same cell: activate (inclusive)");
    CHECK(menu_click_kind(4, 1000, 4, 1251) == 2, "251 ms: secondary");
    CHECK(menu_click_kind(4, 1000, 4, 1700) == 2, "700 ms: secondary (inclusive)");
    CHECK(menu_click_kind(4, 1000, 4, 1701) == 0, "701 ms: too slow, remember");
    CHECK(menu_click_kind(4, 1000, 5, 1100) == 0, "a different cell never counts");
    CHECK(menu_cycle(0, 3) == 1 && menu_cycle(2, 3) == 0, "cycle wraps");

    /* differential vs SHANKPIT's own C: every size, every selection, every direction */
    int nav_bad = 0, nav_n = 0;
    for (int n = 1; n <= 24; n++)
        for (int sel = 0; sel < n; sel++)
            for (int dx = -1; dx <= 1; dx++)
                for (int dy = -1; dy <= 1; dy++) {
                    nav_n++;
                    if (menu_nav_move(sel, n, 2, dx, dy) != ref_nav(sel, n, dx, dy)) nav_bad++;
                }
    CHECK(nav_bad == 0, "nav matches SHANKPIT's lobby_nav_move on every size/selection/direction");
    printf("      (%d nav cases)\n", nav_n);

    Lay lay = { 360.0f, 450.0f, 640.0f, 105.0f, 88.0f };
    int hit_bad = 0, hit_n = 0;
    for (int n = 1; n <= 12; n++)
        for (float my = 0.0f; my <= 520.0f; my += 3.0f)
            for (float mx = 340.0f; mx <= 1120.0f; mx += 3.0f) {
                hit_n++;
                if (menu_hit_test((double)mx, (double)my, n, 2, 360.0, 450.0, 640.0, 105.0, 88.0) != ref_hit(mx, my, n, &lay)) hit_bad++;
            }
    CHECK(hit_bad == 0, "hit-test matches SHANKPIT's lobby_hit_test on a dense pointer sweep");
    printf("      (%d pointer positions)\n", hit_n);

    int click_bad = 0, click_n = 0;
    for (int li = -1; li <= 3; li++)
        for (unsigned lm = 0; lm <= 1500; lm += 50)
            for (int idx = 0; idx <= 3; idx++)
                for (unsigned now = lm; now <= lm + 900; now += 25) {
                    click_n++;
                    if (menu_click_kind(li, (int)lm, idx, (int)now) != ref_click(li, lm, idx, now)) click_bad++;
                }
    CHECK(click_bad == 0, "click timing matches the lobby's thresholds on a dense sweep");
    printf("      (%d click cases)\n", click_n);

    /* ---- submenu path (card #497), hand-derived ---- */
    CHECK(menu_path_depth(0) == 0 && menu_path_leaf(0) == -1, "root: depth 0, leaf -1");
    CHECK(menu_path_push(0, 3) == 4 && menu_path_depth(4) == 1 && menu_path_leaf(4) == 3, "push 3 from root = 4, depth 1, leaf 3");
    CHECK(menu_path_push(4, 0) == 513 && menu_path_depth(513) == 2 && menu_path_leaf(513) == 0, "root->3->0 packs to 513");
    CHECK(menu_path_pop(513) == 4 && menu_path_pop(4) == 0 && menu_path_pop(0) == 0, "pop unwinds, root pop is a no-op");
    CHECK(menu_path_parent_sel(513) == 0 && menu_path_parent_sel(menu_path_pop(513)) == 3, "re-select the folder just left");
    CHECK(menu_path_push(0, -1) == 0 && menu_path_push(0, 127) == 0 && menu_path_push(0, 126) == 127, "out-of-range cells refused, 126 is the max");
    {
        int p = 0; for (int i = 0; i < 4; i++) p = menu_path_push(p, 126);
        CHECK(menu_path_depth(p) == 4 && p > 0, "4 levels of max cell stay positive (28 bits)");
        CHECK(menu_path_push(p, 1) == p, "5th level refused");
    }
    CHECK(menu_path_enter(0, 2, 1) == 3 && menu_path_enter(0, 2, 0) == 0, "folder descends, action leaves path alone");
    {   /* push/pop round trip over every cell at every depth <= 3 */
        int bad = 0;
        for (int a = 0; a <= 126; a++) for (int b = 0; b <= 126; b += 7) for (int d = 0; d <= 126; d += 31) {
            int p = menu_path_push(menu_path_push(menu_path_push(0, a), b), d);
            if (menu_path_depth(p) != 3 || menu_path_leaf(p) != d || menu_path_leaf(menu_path_pop(p)) != b
                || menu_path_leaf(menu_path_pop(menu_path_pop(p))) != a || menu_path_pop(menu_path_pop(menu_path_pop(p))) != 0) bad++;
        }
        CHECK(bad == 0, "push/pop round trip across the whole cell range");
    }

    printf(failures ? "\n%d FAILED\n" : "\nAll ui/menu checks passed.\n", failures);
    return failures ? 1 : 0;
}
