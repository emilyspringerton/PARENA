#!/usr/bin/env bash
# Cross-check runtime/mlkem (vendored C) against Go's independent crypto/mlkem. Needs Go >= 1.24.
set -euo pipefail
cd "$(dirname "$0")"
R=../../runtime/mlkem
gcc -std=c99 -Wall -Wextra -O2 -I$R -DKYBER_K=3 cli.c $R/kem.c $R/indcpa.c $R/polyvec.c $R/poly.c $R/ntt.c $R/cbd.c $R/reduce.c $R/verify.c $R/fips202.c $R/symmetric-shake.c $R/randombytes.c -o cli
export GOWORK=off
GO="${GO:-go}"
$GO test -count=1 ./...
