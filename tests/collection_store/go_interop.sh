#!/usr/bin/env bash
# tests/collection_store/go_interop.sh -- proves the PARENA event store is on-disk compatible with PRRJECT_FATBABY's
# REAL Go eventstore.FileStore (types.go + file_store.go, taken verbatim from that repo's HEAD with git show):
#   1. PARENA ingests events; Go's NewFileStore + ReadFrom reads every record back, sequences contiguous.
#   2. Go Appends one event; it gets the next sequence.
#   3. PARENA ingests another event; it continues after Go's.
# The only substitution: eventstore/seqlock (cgo + POSIX flock, which does not build on Windows) is a no-op stub.
# This test is one process at a time, so the lock has nothing to exclude.
#
# Environment: PRRJECT_FATBABY_DIR (the repo; required -- skipped when unset or missing), GO (default go),
#              LUA51, PARENA_BIN as for the ingest.  usage: bash tests/collection_store/go_interop.sh <workdir>
set -uo pipefail
cd "$(dirname "$0")/../.."
work="${1:?workdir}"
GO="${GO:-go}"
if [ -z "${PRRJECT_FATBABY_DIR:-}" ] || [ ! -d "$PRRJECT_FATBABY_DIR/.git" ]; then
  echo "go_interop: SKIPPED (PRRJECT_FATBABY_DIR is not a PRRJECT_FATBABY checkout)"; exit 0; fi
if ! command -v "$GO" >/dev/null 2>&1; then echo "go_interop: SKIPPED (no Go toolchain at GO=$GO)"; exit 0; fi

rm -rf "$work" && mkdir -p "$work/mod/eventstore/seqlock" "$work/mod/internal/udsnotify" "$work/mod/cmd/interop"
work="$(cd "$work" && pwd)"
show() { git -C "$PRRJECT_FATBABY_DIR" -c safe.directory='*' show "HEAD:$1"; }
show eventstore/types.go > "$work/mod/eventstore/types.go" || exit 1
show eventstore/file_store.go > "$work/mod/eventstore/file_store.go" || exit 1
show internal/udsnotify/udsnotify.go > "$work/mod/internal/udsnotify/udsnotify.go" || exit 1
gover="$("$GO" env GOVERSION | sed 's/^go//; s/\([0-9]*\.[0-9]*\).*/\1/')"
printf 'module github.com/example/prrject-fatbaby\n\ngo %s\n' "$gover" > "$work/mod/go.mod"
cat > "$work/mod/eventstore/seqlock/seqlock.go" <<'EOF'
// Test stub: the real package is cgo + POSIX flock. Single-process interop test, nothing to exclude.
package seqlock

func Acquire(path string) (int, bool) { return 1, true }
func Release(fd int) bool             { return true }
EOF
# file_store.go uses the min built-in (Go 1.21+). For an older local toolchain, a shim that only builds there;
# the store's own files stay verbatim.
cat > "$work/mod/eventstore/compat_min.go" <<'EOF'
//go:build !go1.21

package eventstore

func min(a, b int) int {
	if a < b {
		return a
	}
	return b
}
EOF
cat > "$work/mod/cmd/interop/main.go" <<'EOF'
package main

import (
	"context"
	"encoding/json"
	"fmt"
	"os"

	"github.com/example/prrject-fatbaby/eventstore"
)

func main() {
	root, mode := os.Args[1], os.Args[2]
	s, err := eventstore.NewFileStore(root)
	if err != nil { fmt.Println("FAIL open:", err); os.Exit(1) }
	defer s.Close()
	ctx := context.Background()
	if mode == "append" {
		recs, err := s.Append(ctx, eventstore.Event{ID: "go:1", Type: "go.interop", Source: "go", Data: json.RawMessage(`{"from":"go"}`)})
		if err != nil { fmt.Println("FAIL append:", err); os.Exit(1) }
		fmt.Printf("appended %d\n", recs[0].Sequence)
		return
	}
	recs, err := s.ReadFrom(ctx, 1, 100000)
	if err != nil { fmt.Println("FAIL read:", err); os.Exit(1) }
	latest, _ := s.LatestSequence(ctx)
	for i, r := range recs {
		if r.Sequence != uint64(i+1) { fmt.Printf("FAIL gap at %d: sequence %d\n", i, r.Sequence); os.Exit(1) }
		if r.Event.ID == "" || r.Event.Type == "" || len(r.Event.Data) == 0 || r.Event.IngestedAt.IsZero() || r.AppendedAt.IsZero() {
			fmt.Printf("FAIL incomplete record %d\n", r.Sequence); os.Exit(1)
		}
	}
	fmt.Printf("read %d latest %d last %s\n", len(recs), latest, recs[len(recs)-1].Event.ID)
}
EOF
(cd "$work/mod" && GOWORK=off "$GO" build -buildvcs=false -o "$work/interop.exe" ./cmd/interop) || { echo "FAIL: the real Go eventstore did not build"; exit 1; }

export COLLECTION_STORE="$work/store" COLLECTION_EVENTS="$work/events.ndjson"
ev() { printf '{"id":"Emily-Forever:%s","type":"reflux.2010","source":"collection/addon-2","occurred_at":"2026-11-13T12:00:00Z","partition_key":"Emily-Forever","data":{"seq":%s},"metadata":{"reader":"parena/journal_reader"}}\n' "$1" "$1"; }
pass=0 fail=0
ok() { if [ "$1" = "$2" ]; then pass=$((pass+1)); else fail=$((fail+1)); echo "FAIL: $3 (got '$1', want '$2')"; fi; }

{ ev 1; ev 2; ev 3; } > "$COLLECTION_EVENTS"
bash scripts/collection-store-ingest.sh >/dev/null
ok "$("$work/interop.exe" "$COLLECTION_STORE" read)" "read 3 latest 3 last Emily-Forever:3" "Go reads PARENA's three records"
ok "$("$work/interop.exe" "$COLLECTION_STORE" append)" "appended 4" "Go appends after PARENA at 4"
ev 4 >> "$COLLECTION_EVENTS"
out="$(bash scripts/collection-store-ingest.sh)"
case "$out" in *"1 appended (sequence 5-5)"*) r=y ;; *) r="$out" ;; esac
ok "$r" y "PARENA continues after Go's append at 5"
ok "$("$work/interop.exe" "$COLLECTION_STORE" read)" "read 5 latest 5 last Emily-Forever:4" "Go reads all five, contiguous"
echo "go_interop: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
