/* tests/test_math_harmonics.c -- verification for the pure-PARENA angular-harmonic helpers in
 * stdlib/math/math.prn (cos-2t, sin-2t, cos-3t, cos-5t; card #446). Expected values are hand-derived
 * from exact angles (t = 0, 60deg, 90deg, 180deg), not from calling libm on the same formulas. */
#include "parena_runtime.h"
#include <stdio.h>
#include <math.h>

#include "test_math_harmonics_gen.c"

static int failures = 0;
#define NEAR(a, b, msg) do { \
    if (fabs((a) - (b)) > 1e-9) { printf("FAIL: %s (got %g want %g)\n", msg, (double)(a), (double)(b)); failures++; } \
    else { printf("PASS: %s\n", msg); } \
} while (0)

int main(void) {
    /* t = 0: c=1 s=0 -> everything 1 except sin 2t = 0 */
    NEAR(cos_2t(1.0, 0.0), 1.0, "cos 2t at t=0");
    NEAR(sin_2t(1.0, 0.0), 0.0, "sin 2t at t=0");
    NEAR(cos_3t(1.0), 1.0, "cos 3t at t=0");
    NEAR(cos_5t(1.0), 1.0, "cos 5t at t=0");
    /* t = 90deg: c=0 s=1 -> cos2t=-1, sin2t=0, cos3t=0, cos5t=0 */
    NEAR(cos_2t(0.0, 1.0), -1.0, "cos 2t at t=90deg");
    NEAR(sin_2t(0.0, 1.0), 0.0, "sin 2t at t=90deg");
    NEAR(cos_3t(0.0), 0.0, "cos 3t at t=90deg");
    NEAR(cos_5t(0.0), 0.0, "cos 5t at t=90deg");
    /* t = 60deg: c=0.5 s=sqrt(3)/2 -> cos2t=-0.5, sin2t=sqrt(3)/2, cos3t=-1, cos5t=0.5 (cos 300deg) */
    NEAR(cos_2t(0.5, 0.8660254037844386), -0.5, "cos 2t at t=60deg");
    NEAR(sin_2t(0.5, 0.8660254037844386), 0.8660254037844386, "sin 2t at t=60deg");
    NEAR(cos_3t(0.5), -1.0, "cos 3t at t=60deg");
    NEAR(cos_5t(0.5), 0.5, "cos 5t at t=60deg");
    /* t = 180deg: c=-1 s=0 -> cos2t=1, cos3t=-1, cos5t=-1 */
    NEAR(cos_2t(-1.0, 0.0), 1.0, "cos 2t at t=180deg");
    NEAR(cos_3t(-1.0), -1.0, "cos 3t at t=180deg");
    NEAR(cos_5t(-1.0), -1.0, "cos 5t at t=180deg");
    printf("%s (%d failures)\n", failures ? "FAILED" : "ALL PASSED", failures);
    return failures ? 1 : 0;
}
