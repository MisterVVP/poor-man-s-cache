#!/bin/bash
set -euo pipefail

valgrind --leak-check=full --error-exitcode=1 /app/poor-man-s-cache &
SERVER_PID=$!
trap 'kill $SERVER_PID 2>/dev/null || true' EXIT

# wait for server to start
for i in {1..50}; do
  if nc -z localhost 9001; then
    break
  fi
  sleep 0.1
done

for i in $(seq 1 10000); do
  printf "SET key$i value$i\x1F" | nc localhost 9001 >/dev/null || true
done

kill $SERVER_PID 2>/dev/null || true
wait $SERVER_PID
trap - EXIT
