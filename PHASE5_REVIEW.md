# Phase 5 Review — Per-Process Observability and Hot-Shard Detection

## Verdict
Current branch is **partially compliant** with Phase 5 requirements.

## Requirement-by-requirement status

### 1) Expose `/metrics` per worker with required signals
**Status: Partial**

Implemented today:
- Per-worker labels (`shard`, `node`) are emitted in Prometheus metrics.
- Request/response counters, bytes RX/TX, connection counts, write queue depth, read buffer usage, KVS footprint, and batch counters are exported.

Missing vs requested:
- Explicit **in-flight** request metric.
- Explicit **epolls** and **syscalls** counters.
- **Batch-size histogram** (only count/sum are present).

### 2) Add `/shard` info endpoint
**Status: Missing**

No `/shard` endpoint is implemented in the current server.
Requested fields (worker-index, CPU, NUMA node, NIC queue id, memory arena usage) are not exposed through an HTTP endpoint.

### 3) Add debug-gated hot-key sampler
**Status: Missing**

There is no hot-key sampler implementation (e.g., top-N key hash tracking) and no corresponding debug flag gate.

### 4) Acceptance: dashboard supports hotspot identification
**Status: Partial**

There is an existing Grafana dashboard with per-shard timeseries (request rate, bytes, queue, batch rate/avg), which helps spot imbalance.
However, the dashboard does not yet include all Phase 5-requested dimensions (in-flight, epoll/syscall counters, shard metadata endpoint data, hot-key sampling views).

## Suggested next implementation slice
1. Extend `MetricsSnapshot` and render path with:
   - `in_flight_requests` gauge.
   - `epoll_events_total` counter.
   - syscall counters (recv/send/accept/read/write as needed).
   - true Prometheus histogram buckets for batch size.
2. Add `/shard` endpoint in metrics HTTP server returning JSON metadata.
3. Implement optional hot-key sketch + top-N exporter under env/debug flag.
4. Extend Grafana dashboard panels to per-worker heatmap and hot-key panels.
