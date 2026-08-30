/* tests/test_datetime.c -- real end-to-end verification of stdlib/datetime.prn (founder
 * real-time: "add stdlibs for date time use the same magic string as golang").
 *
 * Real, honest wiring note: <time.h> is included AFTER parena_runtime.h, not before --
 * parena_runtime.h's own _POSIX_C_SOURCE must be the first thing defined before any system
 * header is included (see its own header comment); datetime.prn's generated C doesn't include
 * <time.h> itself, so this file supplies it once the POSIX feature-test macro is already set.
 */
#include "parena_runtime.h"
#include <time.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>

#include "test_datetime_gen.c"

int main(void) {
    Arena arena;
    arena_init(&arena);

    /* is-leap-year?: real, standard Gregorian edge cases. */
    assert(is_leap_year_(2000) == 1); /* divisible by 400 */
    assert(is_leap_year_(1900) == 0); /* divisible by 100, not 400 */
    assert(is_leap_year_(2024) == 1); /* divisible by 4, not 100 */
    assert(is_leap_year_(2023) == 0);

    /* days-in-month: leap-year-aware February, real month-length spot checks. */
    assert(days_in_month(2024, 2) == 29);
    assert(days_in_month(2023, 2) == 28);
    assert(days_in_month(2024, 1) == 31);
    assert(days_in_month(2024, 4) == 30);

    /* day-of-year: 2024-03-01 = 31 (Jan) + 29 (Feb, leap) + 1 = 61. */
    assert(day_of_year(2024, 3, 1) == 61);

    /* format-go-layout: 2024-01-15 10:30:00 UTC, epoch 1705314600 (hand-verified). */
    char *layout = "2006-01-02 15:04:05";
    char *out = format_go_layout(1705314600, layout, &arena);
    assert(strcmp(out, "2024-01-15 10:30:00") == 0);

    /* Real, literal-passthrough case: unrecognized layout characters pass through unchanged. */
    char *literal_layout = "Year: 2006";
    char *literal_out = format_go_layout(1705314600, literal_layout, &arena);
    assert(strcmp(literal_out, "Year: 2024") == 0);

    printf("test_datetime: all assertions passed\n");
    return 0;
}
