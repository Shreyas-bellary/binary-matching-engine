#!/usr/bin/env bash
set -euo pipefail

ENGINE_BIN="${ENGINE_BIN:-/app/engine}"
GATEWAY_DIR="${GATEWAY_DIR:-/app/gateway}"
SOCKET_PATH="${ENGINE_SOCKET:-/tmp/exchange.sock}"

cleanup() {
  echo "[start.sh] shutting down..."
  if [[ -n "${ENGINE_PID:-}" ]] && kill -0 "$ENGINE_PID" 2>/dev/null; then
    kill "$ENGINE_PID" 2>/dev/null || true
    wait "$ENGINE_PID" 2>/dev/null || true
  fi
  rm -f "$SOCKET_PATH"
}
trap cleanup EXIT INT TERM

rm -f "$SOCKET_PATH"

echo "[start.sh] launching matching engine: $ENGINE_BIN"
"$ENGINE_BIN" &
ENGINE_PID=$!

for i in $(seq 1 50); do
  if [[ -S "$SOCKET_PATH" ]]; then
    echo "[start.sh] engine socket ready at $SOCKET_PATH"
    break
  fi
  if ! kill -0 "$ENGINE_PID" 2>/dev/null; then
    echo "[start.sh] engine exited unexpectedly" >&2
    exit 1
  fi
  sleep 0.1
done

if [[ ! -S "$SOCKET_PATH" ]]; then
  echo "[start.sh] timed out waiting for engine socket" >&2
  exit 1
fi

echo "[start.sh] launching REST gateway"
cd "$GATEWAY_DIR"
exec node dist/server.js
