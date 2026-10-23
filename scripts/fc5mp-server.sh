#!/bin/bash
# FC5MP server launcher — starts socat (TLS termination) + fc5mp-server
set -euo pipefail

SERVER_DIR="$HOME/fc5mp/server"
CERTS_DIR="$SERVER_DIR/certs"
REST_PORT="${FC5MP_REST_PORT:-21000}"
BACKEND_PORT=$((REST_PORT + 1))

if [ ! -f "$CERTS_DIR/combined.pem" ]; then
    echo "[fc5mp] Generating self-signed TLS cert..."
    mkdir -p "$CERTS_DIR"
    openssl req -x509 -newkey rsa:2048 \
        -keyout "$CERTS_DIR/key.pem" -out "$CERTS_DIR/cert.pem" \
        -days 365 -nodes -subj "/CN=fc5mp-local" 2>/dev/null
    cat "$CERTS_DIR/key.pem" "$CERTS_DIR/cert.pem" > "$CERTS_DIR/combined.pem"
fi

cleanup() {
    echo "[fc5mp] Shutting down..."
    kill "$SERVER_PID" 2>/dev/null || true
    wait 2>/dev/null
}
trap cleanup EXIT INT TERM

# No socat: the server terminates TLS natively on REST_PORT via OpenSSL
# (certs/cert.pem + certs/key.pem). socat's relay was dropping the game's
# HTTPS responses, so it was removed.
echo "[fc5mp] Starting fc5mp-server (native TLS :${REST_PORT}, PRUDP :6570)"
cd "$SERVER_DIR"
RUST_LOG=fc5mp_server=debug cargo run -- "$@" &
SERVER_PID=$!

echo "[fc5mp] Server ready. Ctrl+C to stop."
echo "[fc5mp]   TLS:   0.0.0.0:${REST_PORT}  (native, in-process)"
echo "[fc5mp]   PRUDP: 0.0.0.0:6570"
wait
