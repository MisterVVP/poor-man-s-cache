#!/usr/bin/env bash
set -euo pipefail

# Configuration
# =============

# Number of worker processes (ideally == number of physical cores)
WORKER_COUNT="${WORKER_COUNT:-8}"

# Base port for multi-port mode (if REUSEPORT is disabled)
BASE_PORT="${BASE_PORT:-7001}"

# If true, all workers listen on the same port using SO_REUSEPORT.
# If false, workers listen on BASE_PORT + worker_index
USE_REUSEPORT="${USE_REUSEPORT:-1}"

# Path to built binary
BIN_PATH="${BIN_PATH:-./out/build/Release/src/poor-man-s-cache}"

# Optional .env file to source (for NUM_SHARDS, PROMETHEUS_PORT, etc.)
ENV_FILE="${ENV_FILE:-.env}"

# NUMA / CPU affinity
USE_NUMA="${USE_NUMA:-0}"   # 0 = off, 1 = use numactl --cpunodebind/--membind
START_CPU="${START_CPU:-0}" # first CPU index to use

# Logging
LOG_DIR="${LOG_DIR:-./logs/workers}"
mkdir -p "$LOG_DIR"

# Helper: resolve listen port for a worker
get_port_for_worker() {
  local idx="$1"
  if [[ "$USE_REUSEPORT" == "1" ]]; then
    # All workers share the same TCP port
    echo "${BASE_PORT}"
  else
    # Multi-port mode
    echo $((BASE_PORT + idx))
  fi
}

# Helper: map CPU index to NUMA node (simple heuristic using numactl)
get_numa_node_for_cpu() {
  local cpu="$1"
  if ! command -v numactl >/dev/null 2>&1; then
    echo "0"
    return
  fi
  # Fallback: just use 0 if detection fails
  local node
  node="$(numactl --show 2>/dev/null | awk '/nodebind:/ {print $2}' | head -n1 || echo 0)"
  echo "${node:-0}"
}

# Load env if present
if [[ -f "$ENV_FILE" ]]; then
  # shellcheck disable=SC1090
  source "$ENV_FILE"
fi

# Export PM_REUSEPORT to inform server about reuseport setting
if [[ "$USE_REUSEPORT" == "1" ]]; then
  export PM_REUSEPORT=1
else
  unset PM_REUSEPORT || true
fi

echo "Starting $WORKER_COUNT workers from $BIN_PATH"
echo "USE_REUSEPORT=$USE_REUSEPORT BASE_PORT=$BASE_PORT"

pids=()

for ((i=0; i<WORKER_COUNT; i++)); do
  cpu=$((START_CPU + i))
  port="$(get_port_for_worker "$i")"

  # Per-worker env overrides
  export PORT="$port"
  export WORKER_INDEX="$i"
  export WORKER_COUNT="$WORKER_COUNT"

  log_file="${LOG_DIR}/worker_${i}.log"

  cmd=( "$BIN_PATH" )

  # Wrap in taskset / numactl if requested
  if [[ "$USE_NUMA" == "1" ]]; then
    numa_node="$(get_numa_node_for_cpu "$cpu")"
    cmd=( numactl --cpunodebind="${numa_node}" --membind="${numa_node}" "${cmd[@]}" )
  fi

  cmd=( taskset -c "$cpu" "${cmd[@]}" )

  echo "Launching worker $i on CPU $cpu, port $port, log=$log_file"
  # Run in background, redirecting stdio
  "${cmd[@]}" >"$log_file" 2>&1 &

  pids+=( "$!" )
done

echo "All workers started: ${pids[*]}"
echo "Press Ctrl+C to stop."

# Wait for all workers; handle Ctrl+C cleanly
trap 'echo "Stopping workers..."; kill "${pids[@]}" 2>/dev/null || true; wait || true; exit 0' INT TERM

wait
