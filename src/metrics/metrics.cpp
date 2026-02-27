#include "metrics.hpp"

#if defined(__GLIBC__)
#include <features.h>
#endif
#include <malloc.h>
#include <algorithm>
#include <array>
#include <charconv>
#include <sstream>

using namespace metrics;

namespace {

constexpr std::array<uint64_t, MetricsSnapshot::BatchHistogramBucketCount> BATCH_BUCKET_BOUNDS{1, 2, 4, 8, 16, 32, 64, 128};

std::string buildLabels(std::string_view shard, std::string_view node, std::string_view extra = {}) {
    std::ostringstream oss;
    oss << '{';
    if (!extra.empty()) {
        oss << extra << ',';
    }
    oss << "shard=\"" << shard << "\",node=\"" << node << "\"}";
    return oss.str();
}

void appendMetric(std::ostringstream& oss, std::string_view name, std::string_view labels, uint64_t value) {
    oss << name << labels << ' ' << value << '\n';
}

std::size_t batchBucketFor(std::size_t requestsInBatch) {
    for (std::size_t i = 0; i < BATCH_BUCKET_BOUNDS.size(); ++i) {
        if (requestsInBatch <= BATCH_BUCKET_BOUNDS[i]) {
            return i;
        }
    }
    return BATCH_BUCKET_BOUNDS.size();
}

uint64_t readArenaBytes() {
#if defined(__GLIBC__) && defined(__GLIBC_PREREQ)
#if __GLIBC_PREREQ(2, 33)
    return static_cast<uint64_t>(mallinfo2().uordblks);
#else
    return static_cast<uint64_t>(mallinfo().uordblks);
#endif
#else
    return 0;
#endif
}



} // namespace

MetricsCollector::MetricsCollector(std::string shardLabel, std::string nodeLabel, ShardInfo info, bool samplerEnabled, std::size_t topN)
    : shard(std::move(shardLabel)), node(std::move(nodeLabel)), shardInfo(std::move(info)), hotKeySamplerEnabled(samplerEnabled), hotKeyTopN(std::max<std::size_t>(1, topN)) {}

void MetricsCollector::incrementRequest(RequestOperation op) noexcept {
    std::scoped_lock lock(mutex);
    switch (op) {
        case RequestOperation::Get:
            ++metrics.requestsGet;
            break;
        case RequestOperation::Set:
            ++metrics.requestsSet;
            break;
        case RequestOperation::Del:
            ++metrics.requestsDel;
            break;
        case RequestOperation::Other:
        default:
            ++metrics.requestsOther;
            break;
    }
}

void MetricsCollector::incrementResponse(ResponseStatus status) noexcept {
    std::scoped_lock lock(mutex);
    switch (status) {
        case ResponseStatus::Ok:
            ++metrics.responsesOk;
            break;
        case ResponseStatus::NotFound:
            ++metrics.responsesNotFound;
            break;
        case ResponseStatus::Error:
        default:
            ++metrics.responsesError;
            break;
    }
}

void MetricsCollector::addBytesRx(std::size_t amount) noexcept {
    std::scoped_lock lock(mutex);
    metrics.bytesRx += amount;
}

void MetricsCollector::addBytesTx(std::size_t amount) noexcept {
    std::scoped_lock lock(mutex);
    metrics.bytesTx += amount;
}

void MetricsCollector::connectionAccepted() noexcept {
    std::scoped_lock lock(mutex);
    ++metrics.connectionsAccepted;
    ++metrics.connectionsCurrent;
}

void MetricsCollector::connectionClosed(CloseReason reason) noexcept {
    std::scoped_lock lock(mutex);
    if (metrics.connectionsCurrent > 0) {
        --metrics.connectionsCurrent;
    }

    switch (reason) {
        case CloseReason::Client:
            ++metrics.connectionsClosedClient;
            break;
        case CloseReason::Error:
            ++metrics.connectionsClosedError;
            break;
        case CloseReason::Server:
        default:
            ++metrics.connectionsClosedServer;
            break;
    }
}

void MetricsCollector::setWorkerState(WorkerState state) noexcept {
    std::scoped_lock lock(mutex);
    metrics.workerState = static_cast<uint64_t>(state);
}

void MetricsCollector::incrementShutdown(ShutdownReason reason) noexcept {
    std::scoped_lock lock(mutex);
    switch (reason) {
        case ShutdownReason::Sigterm:
            ++metrics.shutdownsSigterm;
            break;
        case ShutdownReason::Sigint:
            ++metrics.shutdownsSigint;
            break;
        case ShutdownReason::Other:
        default:
            ++metrics.shutdownsOther;
            break;
    }
}

void MetricsCollector::incrementDrainTimeout() noexcept {
    std::scoped_lock lock(mutex);
    ++metrics.drainTimeoutTotal;
}

void MetricsCollector::recordBatch(std::size_t requestsInBatch) noexcept {
    std::scoped_lock lock(mutex);
    ++metrics.batchesTotal;
    metrics.requestsPerBatchSum += requestsInBatch;
    ++metrics.requestsPerBatchCount;
    const auto bucket = batchBucketFor(requestsInBatch);
    if (bucket < MetricsSnapshot::BatchHistogramBucketCount) {
        ++metrics.batchHistogram[bucket];
    }
}

void MetricsCollector::setWriteQueueDepth(std::size_t depth) noexcept {
    std::scoped_lock lock(mutex);
    metrics.writeQueueDepth = depth;
}

void MetricsCollector::setReadBufferUsageBytes(std::size_t bytes) noexcept {
    std::scoped_lock lock(mutex);
    metrics.readBufferUsageBytes = bytes;
}

void MetricsCollector::setKvsState(std::size_t items, std::size_t bytesUsed) noexcept {
    std::scoped_lock lock(mutex);
    metrics.kvsItems = items;
    metrics.kvsBytesUsed = bytesUsed;
}

void MetricsCollector::setInFlightRequests(std::size_t inFlight) noexcept {
    std::scoped_lock lock(mutex);
    metrics.inFlightRequests = inFlight;
}

void MetricsCollector::incrementEpollWait(std::size_t eventCount) noexcept {
    std::scoped_lock lock(mutex);
    ++metrics.epollWaitTotal;
    metrics.epollEventsTotal += eventCount;
}

void MetricsCollector::incrementSyscallRecv() noexcept {
    std::scoped_lock lock(mutex);
    ++metrics.syscallRecvTotal;
}

void MetricsCollector::incrementSyscallSend() noexcept {
    std::scoped_lock lock(mutex);
    ++metrics.syscallSendTotal;
}

void MetricsCollector::incrementSyscallAccept() noexcept {
    std::scoped_lock lock(mutex);
    ++metrics.syscallAcceptTotal;
}

void MetricsCollector::sampleKeyHash(uint64_t keyHash) noexcept {
    if (!hotKeySamplerEnabled) {
        return;
    }

    std::scoped_lock lock(mutex);
    for (auto& [hash, count] : metrics.hotKeysTop) {
        if (hash == keyHash) {
            ++count;
            return;
        }
    }

    if (metrics.hotKeysTop.size() < hotKeyTopN) {
        metrics.hotKeysTop.emplace_back(keyHash, 1);
        return;
    }

    auto minIt = std::min_element(metrics.hotKeysTop.begin(), metrics.hotKeysTop.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.second < rhs.second;
    });
    if (minIt == metrics.hotKeysTop.end()) {
        ++metrics.hotKeySamplesDropped;
        return;
    }

    const uint64_t minCount = minIt->second;
    minIt->first = keyHash;
    minIt->second = minCount + 1;
}

MetricsSnapshot MetricsCollector::snapshot() const noexcept {
    std::scoped_lock lock(mutex);
    return metrics;
}

std::string MetricsCollector::renderPrometheus() const {
    auto snap = snapshot();
    const auto labels = buildLabels(shard, node);
    std::ostringstream oss;

    appendMetric(oss, "pmc_requests_total", buildLabels(shard, node, "op=\"get\""), snap.requestsGet);
    appendMetric(oss, "pmc_requests_total", buildLabels(shard, node, "op=\"set\""), snap.requestsSet);
    appendMetric(oss, "pmc_requests_total", buildLabels(shard, node, "op=\"del\""), snap.requestsDel);
    appendMetric(oss, "pmc_requests_total", buildLabels(shard, node, "op=\"other\""), snap.requestsOther);

    appendMetric(oss, "pmc_responses_total", buildLabels(shard, node, "status=\"ok\""), snap.responsesOk);
    appendMetric(oss, "pmc_responses_total", buildLabels(shard, node, "status=\"not_found\""), snap.responsesNotFound);
    appendMetric(oss, "pmc_responses_total", buildLabels(shard, node, "status=\"error\""), snap.responsesError);

    appendMetric(oss, "pmc_bytes_rx_total", labels, snap.bytesRx);
    appendMetric(oss, "pmc_bytes_tx_total", labels, snap.bytesTx);

    appendMetric(oss, "pmc_connections_current", labels, snap.connectionsCurrent);
    appendMetric(oss, "pmc_connections_accepted_total", labels, snap.connectionsAccepted);
    appendMetric(oss, "pmc_connections_closed_total", buildLabels(shard, node, "reason=\"client\""), snap.connectionsClosedClient);
    appendMetric(oss, "pmc_connections_closed_total", buildLabels(shard, node, "reason=\"server\""), snap.connectionsClosedServer);
    appendMetric(oss, "pmc_connections_closed_total", buildLabels(shard, node, "reason=\"error\""), snap.connectionsClosedError);
    appendMetric(oss, "pmc_worker_state", labels, snap.workerState);
    appendMetric(oss, "pmc_shutdowns_total", buildLabels(shard, node, "reason=\"sigterm\""), snap.shutdownsSigterm);
    appendMetric(oss, "pmc_shutdowns_total", buildLabels(shard, node, "reason=\"sigint\""), snap.shutdownsSigint);
    appendMetric(oss, "pmc_shutdowns_total", buildLabels(shard, node, "reason=\"other\""), snap.shutdownsOther);
    appendMetric(oss, "pmc_drain_timeout_total", labels, snap.drainTimeoutTotal);

    appendMetric(oss, "pmc_batches_total", labels, snap.batchesTotal);
    appendMetric(oss, "pmc_requests_per_batch_sum", labels, snap.requestsPerBatchSum);
    appendMetric(oss, "pmc_requests_per_batch_count", labels, snap.requestsPerBatchCount);

    appendMetric(oss, "pmc_kvs_items", labels, snap.kvsItems);
    appendMetric(oss, "pmc_kvs_bytes_used", labels, snap.kvsBytesUsed);
    appendMetric(oss, "pmc_write_queue_depth", labels, snap.writeQueueDepth);
    appendMetric(oss, "pmc_read_buffer_usage_bytes", labels, snap.readBufferUsageBytes);
    appendMetric(oss, "pmc_in_flight_requests", labels, snap.inFlightRequests);
    appendMetric(oss, "pmc_epoll_wait_total", labels, snap.epollWaitTotal);
    appendMetric(oss, "pmc_epoll_events_total", labels, snap.epollEventsTotal);
    appendMetric(oss, "pmc_syscall_recv_total", labels, snap.syscallRecvTotal);
    appendMetric(oss, "pmc_syscall_send_total", labels, snap.syscallSendTotal);
    appendMetric(oss, "pmc_syscall_accept_total", labels, snap.syscallAcceptTotal);

    uint64_t cumulative = 0;
    for (std::size_t i = 0; i < BATCH_BUCKET_BOUNDS.size(); ++i) {
        cumulative += snap.batchHistogram[i];
        appendMetric(oss, "pmc_batch_size_bucket", buildLabels(shard, node, std::string("le=\"") + std::to_string(BATCH_BUCKET_BOUNDS[i]) + "\""), cumulative);
    }
    appendMetric(oss, "pmc_batch_size_bucket", buildLabels(shard, node, "le=\"+Inf\""), snap.batchesTotal);
    appendMetric(oss, "pmc_batch_size_sum", labels, snap.requestsPerBatchSum);
    appendMetric(oss, "pmc_batch_size_count", labels, snap.batchesTotal);

    appendMetric(oss, "pmc_hotkey_samples_dropped_total", labels, snap.hotKeySamplesDropped);
    for (const auto& [hash, count] : snap.hotKeysTop) {
        appendMetric(oss, "pmc_hotkey_hash_hits_total", buildLabels(shard, node, std::string("hash=\"") + std::to_string(hash) + "\""), count);
    }

    return oss.str();
}

std::string MetricsCollector::renderShardInfoJson() const {
    const auto snap = snapshot();
    const auto arenaBytes = readArenaBytes();
    std::ostringstream oss;
    oss << "{"
        << "\"worker_index\":\"" << shardInfo.workerIndex << "\"," 
        << "\"cpu\":\"" << shardInfo.cpu << "\"," 
        << "\"numa_node\":\"" << shardInfo.numaNode << "\"," 
        << "\"nic_queue_id\":\"" << shardInfo.nicQueueId << "\"," 
        << "\"memory_arena_bytes\":" << arenaBytes << ","
        << "\"in_flight_requests\":" << snap.inFlightRequests
        << "}";
    return oss.str();
}
