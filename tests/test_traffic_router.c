/* tests/test_traffic_router.c -- real end-to-end verification for
 * stdlib/edge_game/traffic_router.prn (EDGE.GAME S584, the arcade-cabinet
 * hardware fan-out: Windows PC hub <-> Raspberry Pi + Arduino Nano).
 *
 * This module is pure decision logic with no I/O and no real hardware
 * dependency at all -- unlike test_serial.c (which needs a real pty since
 * serial.prn touches actual file descriptors), every one of these checks
 * runs against plain in-process function calls. No "no real hardware in
 * this sandbox" caveat applies here; this is a full, real test of the
 * actual compiled decision logic.
 */
#include "parena_runtime.h"
#include <stdio.h>
#include <string.h>

#include "test_traffic_router_gen.c"

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s\n", msg); failures++; } \
    else { printf("PASS: %s\n", msg); } \
} while (0)

int main(void) {
    /* Sources: 1 = Windows PC, 2 = Arduino Nano, 3 = Raspberry Pi. */
    RouteTarget from_windows = route_for_source(1);
    CHECK(from_windows.tag == RouteTarget_TAG_ToPiAndNano,
          "source 1 (Windows) fans out to both Pi and Nano");

    RouteTarget from_nano = route_for_source(2);
    CHECK(from_nano.tag == RouteTarget_TAG_ToWindows,
          "source 2 (Nano) relays up to Windows");

    RouteTarget from_pi = route_for_source(3);
    CHECK(from_pi.tag == RouteTarget_TAG_ToWindows,
          "source 3 (Pi) relays up to Windows");

    Message windows_msg = Message_new(1, "lights:red");
    RouteTarget via_message = route_message(&windows_msg);
    CHECK(via_message.tag == RouteTarget_TAG_ToPiAndNano,
          "route-message reads Message.source through get-field correctly");

    if (failures == 0) {
        printf("ALL PASS\n");
    } else {
        printf("%d FAILURE(S)\n", failures);
    }
    return failures == 0 ? 0 : 1;
}
