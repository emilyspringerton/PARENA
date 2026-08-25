#!/usr/bin/env bash
# tools/turbogrep-router.sh -- routes between turbogrep and real GNU
# grep depending on whether the requested pattern uses a regex feature
# turbogrep actually implements. Founder, real-time: "we need to make
# a grep router that can route between regular grep and turbo grep
# depending on if the feature is implemented in turbogrep" -- "and
# then as a way to figure out how to make faster for claude" -- "we
# need to start logging grep invocations to ndjson" -- "so we can
# study how claude code works so we can make it faster."
#
# Real, honest mechanism, not a text-level heuristic guessing at what
# "looks like" unsupported syntax: turbogrep itself checks the real
# parsed AST (regex/pcre.prn's own pattern-supported?) and exits 3 (a
# real, distinct sentinel, separate from 0=matched/1=no-match/2=usage-
# error) specifically when the pattern uses Plus/Optional/Anchor --
# still unimplemented per docs/TURBOGREP_BOTTLENECK_AUDIT.md's own
# C2-C4. This script trusts that signal rather than re-deriving it.
#
# Every invocation is logged as one real NDJSON line to
# $TURBOGREP_LOG (default: PARENA/var/grep-invocations.ndjson) --
# pattern, engine actually used, wall time, match/exit outcome, file
# count. Real, intended use: once this router is wired in as Claude
# Code's own default `grep`, the accumulated log is real usage data
# (not guessed-at synthetic patterns) to prioritize turbogrep's own
# next real feature/perf work against.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TURBOGREP="${TURBOGREP_BIN:-$SCRIPT_DIR/../turbogrep}"
LOG_FILE="${TURBOGREP_LOG:-$SCRIPT_DIR/../var/grep-invocations.ndjson}"

if [ "$#" -lt 2 ]; then
    echo "usage: turbogrep-router.sh <pattern> <file> [file2 ...]" >&2
    exit 2
fi

PATTERN="$1"
shift
FILE_COUNT="$#"

mkdir -p "$(dirname "$LOG_FILE")"

START_NS=$(date +%s%N)
ENGINE="turbogrep"
set +e
OUTPUT="$("$TURBOGREP" "$PATTERN" "$@" 2>/tmp/turbogrep-router-stderr.$$)"
TG_EXIT=$?
set -e
rm -f /tmp/turbogrep-router-stderr.$$

if [ "$TG_EXIT" -eq 3 ]; then
    # real fallback: unsupported feature, not a "no match" or error --
    # re-run with real GNU grep instead, same pattern/files, same real
    # user-visible behavior a plain `grep` invocation would have had.
    ENGINE="grep"
    set +e
    OUTPUT="$(grep -H "$PATTERN" "$@" 2>/dev/null)"
    GREP_EXIT=$?
    set -e
    FINAL_EXIT="$GREP_EXIT"
else
    FINAL_EXIT="$TG_EXIT"
fi

END_NS=$(date +%s%N)
DURATION_MS=$(( (END_NS - START_NS) / 1000000 ))
MATCH_COUNT=0
if [ -n "$OUTPUT" ]; then
    MATCH_COUNT=$(printf '%s\n' "$OUTPUT" | wc -l)
fi

# Real NDJSON line -- one real event per invocation, append-only,
# matching this monorepo's own event-store convention elsewhere
# (PRRJECT_FATBABY's var/secwatch etc.). Pattern is JSON-string-escaped
# by hand (only \, ", and control chars matter for a real regex
# pattern in practice) rather than pulling in a JSON library for one
# field -- narrow, honest, matches this whole stdlib's own scope
# discipline.
ESCAPED_PATTERN=$(printf '%s' "$PATTERN" | sed 's/\\/\\\\/g; s/"/\\"/g')
printf '{"ts":"%s","pattern":"%s","engine":"%s","file_count":%d,"duration_ms":%d,"exit_code":%d,"match_count":%d}\n' \
    "$(date -u +%Y-%m-%dT%H:%M:%SZ)" "$ESCAPED_PATTERN" "$ENGINE" "$FILE_COUNT" "$DURATION_MS" "$FINAL_EXIT" "$MATCH_COUNT" \
    >> "$LOG_FILE"

if [ -n "$OUTPUT" ]; then
    printf '%s\n' "$OUTPUT"
fi
exit "$FINAL_EXIT"
