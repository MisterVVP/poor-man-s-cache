#!/bin/bash
set -euo pipefail

# Load required environment variables and limit test iterations
source "$(dirname "$0")/.env"
# Fewer iterations keep valgrind runs short for CI
export TEST_ITERATIONS=100
export TEST_POOL_SIZE=1

VALGRIND_LOG="$(mktemp)"
valgrind \
  --leak-check=full \
  --show-leak-kinds=definite,indirect,possible \
  --errors-for-leak-kinds=definite,indirect,possible \
  --error-exitcode=1 \
  --log-file="${VALGRIND_LOG}" \
  /app/poor-man-s-cache &
SERVER_PID=$!
trap 'kill $SERVER_PID 2>/dev/null || true' EXIT

# wait for server to start
for i in {1..50}; do
  if nc -z localhost 9001; then
    break
  fi
  sleep 0.1
done

set +e
# Limit the test run to avoid CI timeouts
timeout 8m python3 /tests/tcp_server_test.py -p
TEST_STATUS=$?
set -e

kill $SERVER_PID 2>/dev/null || true
set +e
wait $SERVER_PID
VALGRIND_STATUS=$?
set -e
trap - EXIT
grep -v "still reachable" "${VALGRIND_LOG}" || true
exit $((TEST_STATUS || VALGRIND_STATUS))
