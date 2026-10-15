#!/usr/bin/env bash
# End-to-end QUIC/HTTP3 client test against aioquic. usage: run_quic_client_test.sh <test-binary>
# Needs: python3 + aioquic (pip install aioquic), openssl. Skips (exit 0) when they are missing.
set -u
BIN="$1"
HERE="$(cd "$(dirname "$0")" && pwd)"
python3 -c "import aioquic" 2>/dev/null || { echo "SKIP: python3 aioquic not importable (pip install aioquic)"; exit 0; }
command -v openssl >/dev/null || { echo "SKIP: openssl not found"; exit 0; }
W="$(mktemp -d)"; PIDS=""
trap 'kill $PIDS 2>/dev/null; rm -rf "$W"' EXIT
cd "$W"
openssl req -x509 -newkey ec -pkeyopt ec_paramgen_curve:prime256v1 -nodes -keyout ca.key -out ca.pem -days 2 -subj "/CN=H3 Test CA" 2>/dev/null
mk() { # name san
  openssl req -newkey ec -pkeyopt ec_paramgen_curve:prime256v1 -nodes -keyout "$1.key" -out "$1.csr" -subj "/CN=$1" 2>/dev/null
  printf "subjectAltName=%s\n" "$2" > "$1.ext"
  openssl x509 -req -in "$1.csr" -CA ca.pem -CAkey ca.key -CAcreateserial -out "$1.pem" -days 2 -extfile "$1.ext" 2>/dev/null
}
mk good "DNS:localhost,IP:127.0.0.1"
mk bad  "DNS:other.example"
base=$((20000 + RANDOM % 20000))
start() { "$@" > /dev/null 2>&1 & PIDS="$PIDS $!"; }
start python3 "$HERE/h3_server.py" $base      good.pem good.key
start python3 "$HERE/h3_server.py" $((base+1)) bad.pem  bad.key
start python3 "$HERE/udp_proxy.py" $((base+2)) $base --drop-c2s 0 --drop-s2c-every 7
start python3 "$HERE/udp_proxy.py" $((base+3)) $base --reorder-s2c
sleep 2
rc=0
export SSL_CERT_FILE="$W/ca.pem" ASAN_OPTIONS=detect_leaks=0
echo "== happy path (cert trusted via SSL_CERT_FILE only)"; "$BIN" 127.0.0.1 $base ok || rc=1
echo "== wrong certificate name must be rejected";           "$BIN" 127.0.0.1 $((base+1)) reject || rc=1
echo "== untrusted CA must be rejected"; SSL_CERT_FILE=/etc/ssl/certs/ca-certificates.crt "$BIN" 127.0.0.1 $base reject || rc=1
echo "== first Initial dropped + every 7th server datagram dropped"; "$BIN" 127.0.0.1 $((base+2)) lossy || rc=1
echo "== server datagrams reordered pairwise";               "$BIN" 127.0.0.1 $((base+3)) lossy || rc=1
echo "== closed UDP port fails fast (ICMP unreachable)";    "$BIN" 127.0.0.1 $((base+9)) refused || rc=1
exit $rc
