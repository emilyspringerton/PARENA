#!/usr/bin/env bash
# tests/test_standings.sh -- end-to-end test for `parena standings`
# (stdlib/league/standings.prn + tools/standings_host.c): serves the
# checked-in fixtures under tests/fixtures/standings/ from a local
# python3 http.server (a stand-in for IDUNA's GET /api/v1/game-checkpoints/<game>),
# then asserts output ordering and every exit code (0/1/3/4), including the
# shell-injection rejections.
set -euo pipefail
cd "$(dirname "$0")/.."
PARENA=./parena
PORT=${STANDINGS_TEST_PORT:-18765}
BASE="http://127.0.0.1:$PORT"
export NO_PROXY=127.0.0.1 no_proxy=127.0.0.1

python3 -m http.server "$PORT" --bind 127.0.0.1 --directory tests/fixtures/standings >/dev/null 2>&1 &
SRV=$!
trap 'kill $SRV 2>/dev/null || true' EXIT
for _ in $(seq 1 50); do curl -s -o /dev/null "$BASE/" && break; sleep 0.1; done

fail=0
expect_rc() { # expect_rc <want> <desc> <cmd...>
    local want=$1 desc=$2; shift 2
    set +e; "$@" >/tmp/standings_out.$$ 2>&1; local rc=$?; set -e
    if [ "$rc" != "$want" ]; then echo "FAIL: $desc (rc=$rc, want $want)"; cat /tmp/standings_out.$$; fail=1
    else echo "ok:   $desc"; fi
}

expect_rc 0 "populated registry" $PARENA standings deadweight --base-url "$BASE"
out=$(cat /tmp/standings_out.$$)
# Role order main -> main_exploiter -> league_exploiter -> others; newest generation first within a role;
# a nested "role" inside another field must not be picked up; an escaped quote in a name must survive.
got=$(echo "$out" | awk '/id=/{print $2}' | tr '\n' ' ')
[ "$got" = "7 3 5 6 8 " ] || { echo "FAIL: ordering, got '$got'"; fail=1; }
echo "$out" | grep -q 'main-g2 "quoted" {x}' || { echo "FAIL: escaped name"; fail=1; }
expect_rc 0 "--top 1 keeps one per role" $PARENA standings deadweight --base-url "$BASE/" --top 1
[ "$(grep -c '  id=' /tmp/standings_out.$$)" = "4" ] || { echo "FAIL: --top 1 row count"; fail=1; }
expect_rc 1 "empty registry (IDUNA_BASE_URL)" env IDUNA_BASE_URL="$BASE" $PARENA standings empty
expect_rc 3 "non-list response" $PARENA standings broken --base-url "$BASE"
expect_rc 3 "404" $PARENA standings missing --base-url "$BASE"
expect_rc 4 "injection in game" $PARENA standings "x';id" --base-url "$BASE"
expect_rc 4 "injection in base url" $PARENA standings deadweight --base-url "$BASE/'\$(id)"
expect_rc 4 "query string rejected" $PARENA standings deadweight --base-url "$BASE/?a=b"
expect_rc 4 "no game" $PARENA standings

rm -f /tmp/standings_out.$$
[ $fail = 0 ] && echo "test_standings: all passed"
exit $fail
