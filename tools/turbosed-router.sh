#!/usr/bin/env bash
# tools/turbosed-router.sh -- routes between turbosed and real GNU sed.
# Same shape as tools/turbogrep-router.sh (study that file's own header
# first if this one is unclear) but for `s/PAT/REPL/[g]`.
#
# NOT installed as $HOME/.local/bin/sed and not intended to be, unlike
# turbogrep's own live $PATH symlink -- real, deliberate rollout
# discipline difference: sed mutates text and drives real pipelines
# across this whole monorepo's build/deploy/CI scripts, so a
# correctness bug here is far more damaging than in grep (which only
# searches). Verified correctness parity on the common literal-
# substitution case (default first-match and /g global, both diff-
# clean against real GNU sed across this repo's own real files -- see
# docs/TURBOSED_VERIFICATION_REPORT.md), but ALSO found a real, honest,
# unresolved gap: global-mode replace against a zero-width-capable
# pattern (e.g. bare `a*`) diverges from real sed (confirmed live,
# documented in that same report) -- not silently wired into $PATH
# until that's actually fixed and re-verified. Invoke this script
# directly (or symlink it yourself) if you want to use it knowingly.
#
# Real, layered safety, in order:
#  1. Any flag argument, or not exactly 2 positional args (script +
#     file -- turbosed supports neither stdin nor multiple files) --
#     delegates the WHOLE invocation to real sed immediately.
#  2. The one positional "script" arg must match `s<DELIM>...<DELIM>...
#     <DELIM>[g]` (turbosed's own parse_sed_expr shape) -- anything
#     else (address ranges, d/p/other commands, multiple -e scripts)
#     delegates to real sed too.
#  3. Even in that shape, turbosed itself checks the real parsed regex
#     AST (pattern-supported?) and exits 3 for Plus/Optional/Anchor,
#     same real sentinel turbogrep already established -- this script
#     falls back to real sed on exit 3.
#
# Every real invocation logs one NDJSON line to $TURBOSED_LOG (default:
# PARENA/var/sed-invocations.ndjson), same real-usage-over-synthetic-
# benchmark discipline turbogrep-router.sh already established.
set -uo pipefail

REAL_SELF="$(readlink -f "${BASH_SOURCE[0]}")"
SCRIPT_DIR="$(cd "$(dirname "$REAL_SELF")" && pwd)"
TURBOSED="${TURBOSED_BIN:-$SCRIPT_DIR/../turbosed}"
REAL_SED="${TURBOSED_REAL_SED:-/usr/bin/sed}"
LOG_FILE="${TURBOSED_LOG:-$SCRIPT_DIR/../var/sed-invocations.ndjson}"

mkdir -p "$(dirname "$LOG_FILE")" 2>/dev/null || true

log_invocation() {
    # $1=expr $2=engine $3=duration_ms $4=exit_code $5=reason
    local esc_expr
    esc_expr=$(printf '%s' "$1" | sed 's/\\/\\\\/g; s/"/\\"/g')
    printf '{"ts":"%s","expr":"%s","engine":"%s","duration_ms":%s,"exit_code":%s,"reason":"%s"}\n' \
        "$(date -u +%Y-%m-%dT%H:%M:%SZ)" "$esc_expr" "$2" "$3" "$4" "$5" \
        >> "$LOG_FILE" 2>/dev/null || true
}

START_NS=$(date +%s%N)

NEEDS_REAL_SED=0
for arg in "$@"; do
    case "$arg" in
        -*) NEEDS_REAL_SED=1 ;;
    esac
done
if [ "$#" -ne 2 ]; then
    NEEDS_REAL_SED=1
fi

# Layer 2: the one positional script must look like `s<delim>...`.
if [ "$NEEDS_REAL_SED" -eq 0 ]; then
    case "$1" in
        s?*) ;;
        *) NEEDS_REAL_SED=1 ;;
    esac
fi

if [ "$NEEDS_REAL_SED" -eq 1 ]; then
    OUTPUT="$("$REAL_SED" "$@" 2>/dev/null)"
    EXIT_CODE=$?
    END_NS=$(date +%s%N)
    log_invocation "${1:-}" "sed" "$(( (END_NS - START_NS) / 1000000 ))" "$EXIT_CODE" "flags_or_shape"
    [ -n "$OUTPUT" ] && printf '%s\n' "$OUTPUT"
    exit "$EXIT_CODE"
fi

EXPR="$1"
FILE="$2"

OUTPUT="$("$TURBOSED" "$EXPR" "$FILE" 2>/tmp/turbosed-router-stderr.$$)"
TS_EXIT=$?
rm -f /tmp/turbosed-router-stderr.$$

if [ "$TS_EXIT" -eq 3 ] || [ "$TS_EXIT" -eq 2 ]; then
    ENGINE="sed"
    REASON="unsupported_or_malformed"
    OUTPUT="$("$REAL_SED" "$EXPR" "$FILE" 2>/dev/null)"
    FINAL_EXIT=$?
else
    ENGINE="turbosed"
    REASON="ok"
    FINAL_EXIT="$TS_EXIT"
fi

END_NS=$(date +%s%N)
log_invocation "$EXPR" "$ENGINE" "$(( (END_NS - START_NS) / 1000000 ))" "$FINAL_EXIT" "$REASON"

[ -n "$OUTPUT" ] && printf '%s\n' "$OUTPUT"
exit "$FINAL_EXIT"
