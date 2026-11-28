#!/usr/bin/env bash
# test-worldmap: compile stdlib/worldmap/wm_rules.prn to C *and* Go with BURROW, run the same vector table through
# both, and require byte-identical output (and the hand-derived expected output). This is the contract's proof: the
# client (C/wasm) and the server (Go) get the same answers because they are the same source.
set -euo pipefail
cd "$(dirname "$0")/.."
BURROW="${BURROW:-burrow}"; command -v "$BURROW" >/dev/null || BURROW=../BURROW/burrow
W="$(mktemp -d)"; trap 'rm -rf "$W"' EXIT
SRC=stdlib/worldmap/wm_rules.prn
"$BURROW" build $SRC -o "$W/wm.c" >/dev/null
"$BURROW" build $SRC -o "$W/wm.go" >/dev/null

cat > "$W/main.c" <<'C'
#define PARENA_NO_GRAPHICS
#include "parena_runtime.h"
#include <stdio.h>
#include "wm.c"
int main(void){
  printf("job town/idle=%d town/fight=%d field/idle=%d dungeon/idle=%d\n", job_change_allowed(1,0), job_change_allowed(1,1), job_change_allowed(2,0), job_change_allowed(3,0));
  printf("travel delay=%d max=%d r4=%d r5=%d r1=%d r20=%d\n", travel_delay_ms(), travel_fail_max(), travel_roll_fails(4), travel_roll_fails(5), travel_roll_fails(1), travel_roll_fails(20));
  printf("kind 0=%d 1=%d 4=%d 5=%d\n", kind_valid(0), kind_valid(1), kind_valid(4), kind_valid(5));
  printf("footprint in=%d edge=%d out=%d\n", point_in_footprint(60,58,60,58,28,22), point_in_footprint(74,69,60,58,28,22), point_in_footprint(74.5f,58,60,58,28,22));
  printf("circle in=%d edge=%d out=%d\n", point_in_circle(61,58,60,58,3.5f), point_in_circle(63.5f,58,60,58,3.5f), point_in_circle(64,58,60,58,3.5f));
  printf("cast commit=%d,%d,%d interrupt=%d,%d,%d,%d pad=%g\n", cast_commits(599,600), cast_commits(600,600), cast_commits(1000,600), cast_interrupted(100,600,0), cast_interrupted(100,600,1), cast_interrupted(600,600,0), cast_interrupted(700,600,0), ring_pad());
  printf("ring in=%d edge=%d slack=%d out=%d\n", crystal_in_ring(284,60,270,60,14), crystal_in_ring(284.5f,60,270,60,14), crystal_still_in_ring(285.5f,60,270,60,14), crystal_still_in_ring(285.6f,60,270,60,14));
  printf("bound in=%d edge=%d out=%d\n", within_bound(-420,0,420), within_bound(420,-420,420), within_bound(0,420.5f,420));
  return 0; }
C
cat > "$W/main.go.tmpl" <<'G'
package main

import (
	"fmt"
	"probe/burrowgen"
)

func b(v bool) int {
	if v {
		return 1
	}
	return 0
}

func main() {
	g := burrowgen.JobChangeAllowed
	fmt.Printf("job town/idle=%d town/fight=%d field/idle=%d dungeon/idle=%d\n", b(g(1, false)), b(g(1, true)), b(g(2, false)), b(g(3, false)))
	r := burrowgen.TravelRollFails
	fmt.Printf("travel delay=%d max=%d r4=%d r5=%d r1=%d r20=%d\n", burrowgen.TravelDelayMs(), burrowgen.TravelFailMax(), b(r(4)), b(r(5)), b(r(1)), b(r(20)))
	k := burrowgen.KindValid
	fmt.Printf("kind 0=%d 1=%d 4=%d 5=%d\n", b(k(0)), b(k(1)), b(k(4)), b(k(5)))
	f := burrowgen.PointInFootprint
	fmt.Printf("footprint in=%d edge=%d out=%d\n", b(f(60, 58, 60, 58, 28, 22)), b(f(74, 69, 60, 58, 28, 22)), b(f(74.5, 58, 60, 58, 28, 22)))
	c := burrowgen.PointInCircle
	fmt.Printf("circle in=%d edge=%d out=%d\n", b(c(61, 58, 60, 58, 3.5)), b(c(63.5, 58, 60, 58, 3.5)), b(c(64, 58, 60, 58, 3.5)))
	w := burrowgen.WithinBound
	cc, ci := burrowgen.CastCommits, burrowgen.CastInterrupted
	fmt.Printf("cast commit=%d,%d,%d interrupt=%d,%d,%d,%d pad=%g\n", b(cc(599, 600)), b(cc(600, 600)), b(cc(1000, 600)), b(ci(100, 600, false)), b(ci(100, 600, true)), b(ci(600, 600, false)), b(ci(700, 600, false)), burrowgen.RingPad())
	fmt.Printf("ring in=%d edge=%d slack=%d out=%d\n", b(burrowgen.CrystalInRing(284, 60, 270, 60, 14)), b(burrowgen.CrystalInRing(284.5, 60, 270, 60, 14)), b(burrowgen.CrystalStillInRing(285.5, 60, 270, 60, 14)), b(burrowgen.CrystalStillInRing(285.6, 60, 270, 60, 14)))
	fmt.Printf("bound in=%d edge=%d out=%d\n", b(w(-420, 0, 420)), b(w(420, -420, 420)), b(w(0, 420.5, 420)))
}
G
gcc -std=c99 -Wall -Wextra -pedantic -Werror -I runtime -o "$W/c.bin" "$W/main.c" -lm
mkdir -p "$W/go/burrowgen"; cp "$W/wm.go" "$W/go/burrowgen/wm.go"; cp "$W/main.go.tmpl" "$W/go/main.go"
printf 'module probe\n\ngo 1.21\n' > "$W/go/go.mod"
(cd "$W/go" && GOWORK=off go run .) > "$W/go.out"
"$W/c.bin" > "$W/c.out"
cat > "$W/want.out" <<'W'
job town/idle=1 town/fight=0 field/idle=0 dungeon/idle=0
travel delay=742 max=4 r4=1 r5=0 r1=1 r20=0
kind 0=0 1=1 4=1 5=0
footprint in=1 edge=1 out=0
circle in=1 edge=1 out=0
cast commit=0,1,1 interrupt=1,0,0,0 pad=1.5
ring in=1 edge=0 slack=1 out=0
bound in=1 edge=1 out=0
W
diff -u "$W/want.out" "$W/c.out" && echo "C target: matches hand-derived"
diff -u "$W/want.out" "$W/go.out" && echo "Go target: matches hand-derived"
echo "test-worldmap: C and Go agree on every vector"
