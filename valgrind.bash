#!/bin/bash
set -euo pipefail

# Load required environment variables and limit test iterations
source "$(dirname "$0")/.env"
export TEST_ITERATIONS=1000
export TEST_POOL_SIZE=1

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

python3 /tests/tcp_server_test.py -p

kill $SERVER_PID 2>/dev/null || true
wait $SERVER_PID
trap - EXIT
