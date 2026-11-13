#!/usr/bin/env bash
# tests/collection_store/store_test.sh -- the Collection event store (stdlib/collection/eventstore.prn) through its
# real wrapper, scripts/collection-store-ingest.sh. usage: bash tests/collection_store/store_test.sh <workdir>
set -uo pipefail
cd "$(dirname "$0")/../.."
work="${1:?workdir}"
rm -rf "$work" && mkdir -p "$work"
export COLLECTION_STORE="$work/store" COLLECTION_EVENTS="$work/events.ndjson"
pass=0 fail=0
ok() { if [ "$1" = "$2" ]; then pass=$((pass+1)); else fail=$((fail+1)); echo "FAIL: $3 (got '$1', want '$2')"; fi; }
ingest() { bash scripts/collection-store-ingest.sh 2>"$work/err"; }
ev() { printf '{"id":"Emily-Forever:%s","type":"reflux.2010","source":"collection/addon-2","occurred_at":"2026-11-13T12:00:00Z","partition_key":"Emily-Forever","data":{"seq":%s}}\n' "$1" "$1"; }
records() { cat "$COLLECTION_STORE"/events/*.ndjson 2>/dev/null | grep -c '^{"sequence":'; }
state() { tr -d '\r\n' < "$COLLECTION_STORE/state/latest-sequence"; }

# nothing to ingest yet
out="$(ingest)"; ok "$?" 0 "a missing events file is not an error"

# two good events and one invalid (no data)
{ ev 1; ev 2; echo '{"id":"x:9","type":"reflux.1"}'; } > "$COLLECTION_EVENTS"
out="$(ingest)"; ok "$?" 0 "first ingest succeeds"
case "$out" in *"3 events read, 2 appended (sequence 1-2), 0 already stored, 1 invalid"*) r=y ;; *) r="$out" ;; esac
ok "$r" y "first ingest report"
ok "$(records)" 2 "two records stored"
ok "$(state)" 2 "latest-sequence is 2"
ok "$(ls "$COLLECTION_STORE/events" | grep -c '^[0-9]\{4\}-[0-9][0-9]-[0-9][0-9]\.ndjson$')" 1 "journal named YYYY-MM-DD.ndjson"
line="$(head -1 "$COLLECTION_STORE"/events/*.ndjson)"
case "$line" in '{"sequence":1,"event":{"id":"Emily-Forever:1",'*'"ingested_at":"'*'"},"appended_at":"'*'"}') r=y ;; *) r="$line" ;; esac
ok "$r" y "record shape: sequence, event (id first, ingested_at added), appended_at"
ok "$([ -d "$COLLECTION_STORE/state/append.lock.d" ] && echo held || echo free)" free "the lock is released"

# idempotent
out="$(ingest)"
case "$out" in *"0 appended, 2 already stored"*) r=y ;; *) r="$out" ;; esac
ok "$r" y "re-ingesting the same file appends nothing"
ok "$(records)" 2 "still two records"

# new events continue the sequence
{ ev 3; ev 4; } >> "$COLLECTION_EVENTS"
out="$(ingest)"
case "$out" in *"2 appended (sequence 3-4)"*) r=y ;; *) r="$out" ;; esac
ok "$r" y "new events continue at 3"

# a missing state file is recovered from the journals (Go's scanLatestFromJournals)
rm "$COLLECTION_STORE/state/latest-sequence"
ev 5 >> "$COLLECTION_EVENTS"
out="$(ingest)"
case "$out" in *"1 appended (sequence 5-5)"*) r=y ;; *) r="$out" ;; esac
ok "$r" y "missing state file: recovered from the journals"
ok "$(state)" 5 "and the state file is written again"

# the state file wins over the journals when both exist (Go's recoverLatestSequence)
echo 41 > "$COLLECTION_STORE/state/latest-sequence"
ev 6 >> "$COLLECTION_EVENTS"
out="$(ingest)"
case "$out" in *"(sequence 42-42)"*) r=y ;; *) r="$out" ;; esac
ok "$r" y "the state file wins, as in Go"

# a corrupt state file stops the ingest, nothing written
echo garbage > "$COLLECTION_STORE/state/latest-sequence"
ev 7 >> "$COLLECTION_EVENTS"
before="$(records)"
ingest >/dev/null; ok "$?" 1 "a corrupt state file is refused"
ok "$(records)" "$before" "and nothing was appended"
grep -q "refusing to guess" "$work/err"; ok "$?" 0 "the refusal says why"
echo 42 > "$COLLECTION_STORE/state/latest-sequence"

# a partial trailing line (a write in flight / a crash) is skipped, not fatal
printf '{"sequence":43,"event":{"id":"half' >> "$(ls "$COLLECTION_STORE"/events/*.ndjson | tail -1)"
printf '\n' >> "$(ls "$COLLECTION_STORE"/events/*.ndjson | tail -1)"
out="$(ingest)"; ok "$?" 0 "a partial journal line does not stop the ingest"
case "$out" in *"1 appended (sequence 43-43)"*"1 partial journal line(s) skipped"*) r=y ;; *) r="$out" ;; esac
ok "$r" y "event 7 appended, the partial line reported"

# a held lock refuses with exit 4 and touches nothing
mkdir "$COLLECTION_STORE/state/append.lock.d"
ev 8 >> "$COLLECTION_EVENTS"
ingest >/dev/null; ok "$?" 4 "a held lock refuses"
rmdir "$COLLECTION_STORE/state/append.lock.d"

# required environment
env -u COLLECTION_STORE bash scripts/collection-store-ingest.sh >/dev/null 2>&1; ok "$?" 2 "COLLECTION_STORE is required"

echo "$pass passed, $fail failed"
[ "$fail" -eq 0 ]
