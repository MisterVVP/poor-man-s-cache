#!/usr/bin/env bash
set -euo pipefail

# ============================================================
# Configuration
# ============================================================

# Number of worker processes (ideally equal to number of cores)
WORKER_COUNT="${CLUSTER_WORKERS:-8}"

# Base port for shard 0 (shards use BASE_PORT + WORKER_INDEX)
BASE_PORT="${SERVER_PORT:-9001}"

# Path to compiled server binary
BIN_PATH="${BIN_PATH:-./out/build/Release/src/poor-man-s-cache}"

# Optional .env file (for NUM_SHARDS, compression flags, etc.)
ENV_FILE="${ENV_FILE:-.env}"

# CPU pinning; 1 = yes, 0 = no
USE_CPU_PINNING="${USE_CPU_PINNING:-1}"

# First CPU index to start pinning from
START_CPU="${START_CPU:-0}"

# NUMA binding; 1 = use numactl, 0 = off
USE_NUMA="${USE_NUMA:-1}"

# Logs
LOG_DIR="${LOG_DIR:-./logs/workers}"
mkdir -p "$LOG_DIR"

# ============================================================
# Helpers
# ============================================================

get_numa_node_for_cpu() {
    local cpu="$1"

    if [[ "$USE_NUMA" != "1" ]]; then
        echo "0"
        return
    fi

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

    echo "0"
}

# ============================================================
# Bootstrap
# ============================================================

if [[ -f "$ENV_FILE" ]]; then
    # shellcheck disable=SC1090
    source "$ENV_FILE"
fi

echo "Starting ${WORKER_COUNT} workers with base port ${BASE_PORT}"
echo "Binary: ${BIN_PATH}"

pids=()

for ((i=0; i<WORKER_COUNT; i++)); do
    cpu=$((START_CPU + i))
    server_port=$((BASE_PORT + i))
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

    echo "Launching worker ${i} → CPU=${cpu} SERVER_PORT=${server_port} LOG=${log_file}"

    WORKER_INDEX="${i}" \
    WORKER_COUNT="${WORKER_COUNT}" \
    SERVER_PORT="${server_port}" \
    "${cmd[@]}" >"$log_file" 2>&1 &

    pids+=( "$!" )
done

echo "USE_NUMA=${USE_NUMA} USE_CPU_PINNING=${USE_CPU_PINNING}, workers started: ${pids[*]}"
echo "Press Ctrl+C to stop."

# Graceful shutdown
trap 'echo "Stopping workers..."; kill "${pids[@]}" 2>/dev/null || true; wait || true; exit 0' INT TERM

wait
