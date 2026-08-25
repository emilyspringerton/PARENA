#!/usr/bin/env bash
# tools/turbogrep-router.sh -- routes between turbogrep and real GNU
# grep. Founder, real-time: "we need to make a grep router that can
# route between regular grep and turbo grep depending on if the
# feature is implemented in turbogrep" -- "log grep invocations to
# ndjson... so we can study how claude code works so we can make it
# faster" -- "wire it into claude code's bash tool."
#
# Installed as $HOME/.local/bin/grep (earlier in PATH than
# /usr/bin/grep) -- there is no mechanism to scope this to "Claude
# Code's Bash tool only" (the harness snapshots this account's real
# shell environment at session start), so this script IS `grep` for
# this whole account once installed: every interactive terminal, every
# script, every other tool this user runs. It must behave like real
# grep for anything it doesn't specifically know how to route, not
# just for turbogrep's own narrow regex-feature gaps.
#
# Real, layered safety, in order:
#  1. Any flag argument (anything starting with '-'), or fewer than 2
#     positional args (pattern + at least one real file -- stdin
#     usage, `something | grep x`, isn't supported by turbogrep at
#     all) -- delegates the WHOLE invocation to real grep immediately,
#     no turbogrep attempt. turbogrep implements none of real grep's
#     flag surface (-i/-r/-n/-v/-c/-l/-A/-B/-C/-e/stdin/etc) -- only
#     the exact bare `grep PATTERN FILE...` shape is a real candidate.
#  2. Even in that bare shape, turbogrep itself checks the REAL parsed
#     regex AST (pattern-supported?, regex/pcre.prn) and exits 3 (a
#     real, distinct sentinel from 0=matched/1=no-match/2=usage-error)
#     when the pattern uses Plus/Optional/Anchor -- still unimplemented
#     per docs/TURBOGREP_BOTTLENECK_AUDIT.md's own C2-C4. This script
#     trusts that signal and falls back to real grep on exit 3.
#
# Every real invocation -- flag-delegated, unsupported-pattern-
# delegated, or turbogrep-handled -- logs one NDJSON line to
# $TURBOGREP_LOG (default: PARENA/var/grep-invocations.ndjson): real
# usage data, not synthetic, to prioritize turbogrep's own next real
# feature/perf work against.
set -uo pipefail

# Real bug found+fixed here (2026-08-25, first real invocation via the
# installed $HOME/.local/bin/grep symlink -- exit 127, "command not
# found"): `dirname "${BASH_SOURCE[0]}"` does NOT resolve symlinks, so
# invoking this script THROUGH a symlink (its whole real, intended
# install shape) resolved SCRIPT_DIR to the symlink's own directory
# ($HOME/.local/bin), not this script's real location (PARENA/tools/)
# -- TURBOGREP pointed at a real path with nothing there. `readlink -f`
# (or realpath) follows the symlink chain to the real file first.
REAL_SELF="$(readlink -f "${BASH_SOURCE[0]}")"
SCRIPT_DIR="$(cd "$(dirname "$REAL_SELF")" && pwd)"
TURBOGREP="${TURBOGREP_BIN:-$SCRIPT_DIR/../turbogrep}"
REAL_GREP="${TURBOGREP_REAL_GREP:-/usr/bin/grep}"
LOG_FILE="${TURBOGREP_LOG:-$SCRIPT_DIR/../var/grep-invocations.ndjson}"

mkdir -p "$(dirname "$LOG_FILE")" 2>/dev/null || true

log_invocation() {
    # $1=pattern(or "") $2=engine $3=file_count $4=duration_ms $5=exit_code $6=match_count $7=reason
    local esc_pattern
    esc_pattern=$(printf '%s' "$1" | sed 's/\\/\\\\/g; s/"/\\"/g')
    printf '{"ts":"%s","pattern":"%s","engine":"%s","file_count":%s,"duration_ms":%s,"exit_code":%s,"match_count":%s,"reason":"%s"}\n' \
        "$(date -u +%Y-%m-%dT%H:%M:%SZ)" "$esc_pattern" "$2" "$3" "$4" "$5" "$6" "$7" \
        >> "$LOG_FILE" 2>/dev/null || true
}

START_NS=$(date +%s%N)

# Layer 1: any flag, or not enough positional args for the one narrow
# shape turbogrep understands -- real grep handles the whole thing,
# no turbogrep attempt at all.
NEEDS_REAL_GREP=0
for arg in "$@"; do
    case "$arg" in
        -*) NEEDS_REAL_GREP=1 ;;
    esac
done
if [ "$#" -lt 2 ]; then
    NEEDS_REAL_GREP=1
fi

if [ "$NEEDS_REAL_GREP" -eq 1 ]; then
    OUTPUT="$("$REAL_GREP" "$@" 2>/dev/null)"
    EXIT_CODE=$?
    END_NS=$(date +%s%N)
    MATCH_COUNT=0
    [ -n "$OUTPUT" ] && MATCH_COUNT=$(printf '%s\n' "$OUTPUT" | wc -l)
    log_invocation "" "grep" "$#" "$(( (END_NS - START_NS) / 1000000 ))" "$EXIT_CODE" "$MATCH_COUNT" "flags_or_stdin"
    [ -n "$OUTPUT" ] && printf '%s\n' "$OUTPUT"
    exit "$EXIT_CODE"
fi

PATTERN="$1"
shift
FILE_COUNT="$#"

# Layer 2: bare `grep PATTERN FILE...` -- let turbogrep try; its own
# exit 3 means "real regex feature I don't implement," fall back.
OUTPUT="$("$TURBOGREP" "$PATTERN" "$@" 2>/tmp/turbogrep-router-stderr.$$)"
TG_EXIT=$?
rm -f /tmp/turbogrep-router-stderr.$$

if [ "$TG_EXIT" -eq 3 ]; then
    ENGINE="grep"
    REASON="unsupported_regex_feature"
    OUTPUT="$("$REAL_GREP" -H "$PATTERN" "$@" 2>/dev/null)"
    FINAL_EXIT=$?
else
    ENGINE="turbogrep"
    REASON="ok"
    FINAL_EXIT="$TG_EXIT"
fi

END_NS=$(date +%s%N)
MATCH_COUNT=0
[ -n "$OUTPUT" ] && MATCH_COUNT=$(printf '%s\n' "$OUTPUT" | wc -l)
log_invocation "$PATTERN" "$ENGINE" "$FILE_COUNT" "$(( (END_NS - START_NS) / 1000000 ))" "$FINAL_EXIT" "$MATCH_COUNT" "$REASON"

[ -n "$OUTPUT" ] && printf '%s\n' "$OUTPUT"
exit "$FINAL_EXIT"
