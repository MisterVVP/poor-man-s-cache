#!/usr/bin/env bash
set -euo pipefail

# ============================================================
# Configuration
# ============================================================

# Number of worker processes (ideally equal to number of cores)
WORKER_COUNT="${WORKER_COUNT:-8}"

# Single shared port (SO_REUSEPORT will allow multiple listeners)
PORT="${PORT:-7001}"

# Path to compiled server binary
BIN_PATH="${BIN_PATH:-./out/build/Release/src/poor-man-s-cache}"

# Optional .env file (your server already supports it)
ENV_FILE="${ENV_FILE:-.env}"

# Use CPU pinning; 1 = yes, 0 = no
USE_CPU_PINNING="${USE_CPU_PINNING:-1}"

# First CPU index to start pinning from
START_CPU="${START_CPU:-0}"

# Use NUMA bind; 1 = yes, 0 = no
USE_NUMA="${USE_NUMA:-0}"

# Logs
LOG_DIR="${LOG_DIR:-./logs/workers}"
mkdir -p "$LOG_DIR"

# Export this so the server enables SO_REUSEPORT internally (if you ever gate it)
export PM_REUSEPORT=1

# ============================================================
# Helpers
# ============================================================

get_numa_node_for_cpu() {
    local cpu="$1"

    # If NUMA binding is disabled, just return node 0
    if [[ "$USE_NUMA" != "1" ]]; then
        echo "0"
        return
    fi

    # If numactl is available, use its hardware view
    if command -v numactl >/dev/null 2>&1; then
        local node
        node="$(numactl --hardware 2>/dev/null | awk -v c="$cpu" '
            /^node [0-9]+ cpus:/ {
                # example: "node 0 cpus: 0 1 2 3"
                gsub("node ","",$2);
                node=$2;
                for (i = 4; i <= NF; ++i) {
                    if ($i == c) {
                        print node;
                        exit;
                    }
                }
            }
        ')"
        if [[ -n "$node" ]]; then
            echo "$node"
            return
        fi
    fi

    # Fallback if detection fails
    echo "0"
}

# ============================================================
# Bootstrap
# ============================================================

if [[ -f "$ENV_FILE" ]]; then
    # shellcheck disable=SC1090
    source "$ENV_FILE"
fi

echo "Starting ${WORKER_COUNT} workers on port ${PORT}"
echo "Binary: ${BIN_PATH}"

pids=()

for ((i=0; i<WORKER_COUNT; i++)); do
    cpu=$((START_CPU + i))
    log_file="${LOG_DIR}/worker_${i}.log"

    cmd=( "$BIN_PATH" )

    # CPU affinity ------------------------------------------------------------
    if [[ "$USE_CPU_PINNING" == "1" ]]; then
        cmd=( taskset -c "$cpu" "${cmd[@]}" )
    fi

    # NUMA binding ------------------------------------------------------------
    if [[ "$USE_NUMA" == "1" ]]; then
        numa_node="$(get_numa_node_for_cpu "$cpu")"
        cmd=( numactl --cpunodebind="$numa_node" --membind="$numa_node" "${cmd[@]}" )
    fi

    echo "Launching worker $i → CPU=$cpu PORT=$PORT LOG=$log_file"

    # Per-worker environment
    WORKER_INDEX="$i" \
    WORKER_COUNT="$WORKER_COUNT" \
    PORT="$PORT" \
    "${cmd[@]}" >"$log_file" 2>&1 &

    pids+=( "$!" )
done

echo "Workers started: ${pids[*]}"
echo "Press Ctrl+C to stop."

# Graceful shutdown
trap 'echo "Stopping workers..."; kill "${pids[@]}" 2>/dev/null || true; wait || true; exit 0' INT TERM

wait
