#!/bin/sh
# scripts/selfhost-fixpoint.sh -- is PARENA self-compiling? (the dogfood ladder, SHANKOS_LAB_NORTHSTAR.md section 3)
#   stage 0: the C compiler ($PARENA0, default ./.parena-bootstrap) builds selfhost/*.prn into binary parena1
#   stage 1: parena1 compiles the same sources into C; that C is built into parena2
#   stage 2: parena2 compiles the same sources into C again
# Fixpoint = the C parena1 emitted and the C parena2 emitted are byte-identical. Exit 0 only then.
# Env: PARENA0 (stage-0 compiler), CC (default cc), WORK (scratch dir, default a fresh mktemp -d).
set -eu
PARENA0=${PARENA0:-./.parena-bootstrap}; CC=${CC:-cc}; WORK=${WORK:-$(mktemp -d)}
SRC="stdlib/string.prn stdlib/array.prn stdlib/io.prn selfhost/lexer.prn selfhost/parser.prn selfhost/region.prn selfhost/emit.prn selfhost/main.prn"
CFLAGS="-std=c99 -Wall -Wextra -pedantic -Werror -DPARENA_NO_GRAPHICS -I runtime"
build_cli() { # $1 = generated C, $2 = output binary
  mkdir -p "$WORK/b"; cp "$1" "$WORK/b/selfhost_cli_gen.c"; cp selfhost/cli_main.c "$WORK/b/cli_main.c"
  $CC $CFLAGS -I "$WORK/b" "$WORK/b/cli_main.c" runtime/parena_runtime.c -o "$2" -lm
}
echo "stage 0: $PARENA0 builds the self-hosted compiler"
$PARENA0 build $SRC -o "$WORK/gen0.c"; build_cli "$WORK/gen0.c" "$WORK/parena1"
echo "stage 1: parena1 compiles itself"
"$WORK/parena1" build $SRC -o "$WORK/gen1.c"; build_cli "$WORK/gen1.c" "$WORK/parena2"
echo "stage 2: parena2 compiles itself"
"$WORK/parena2" build $SRC -o "$WORK/gen2.c"
if cmp -s "$WORK/gen1.c" "$WORK/gen2.c"; then
  echo "FIXPOINT: gen1 == gen2 ($(wc -c < "$WORK/gen2.c") bytes)"
  if cmp -s "$WORK/gen0.c" "$WORK/gen1.c"; then echo "and the C compiler's output matches too (gen0 == gen1)"
  else echo "note: gen0 != gen1 -- the C compiler and the self-hosted one differ in output (diff $WORK/gen0.c $WORK/gen1.c)"; fi
else
  echo "NOT A FIXPOINT: gen1 != gen2 (diff $WORK/gen1.c $WORK/gen2.c)"; exit 1
fi
