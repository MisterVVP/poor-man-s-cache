#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace metrics {

enum class RequestOperation : uint8_t {
    Get,
    Set,
    Del,
    Other,
};

enum class ResponseStatus : uint8_t {
    Ok,
    NotFound,
    Error,
};

enum class CloseReason : uint8_t {
    Client,
    Server,
    Error,
};

struct MetricsSnapshot {
    uint64_t requestsGet = 0;
    uint64_t requestsSet = 0;
    uint64_t requestsDel = 0;
    uint64_t requestsOther = 0;

    uint64_t responsesOk = 0;
    uint64_t responsesNotFound = 0;
    uint64_t responsesError = 0;

    uint64_t bytesRx = 0;
    uint64_t bytesTx = 0;

    uint64_t connectionsCurrent = 0;
    uint64_t connectionsAccepted = 0;
    uint64_t connectionsClosedClient = 0;
    uint64_t connectionsClosedServer = 0;
    uint64_t connectionsClosedError = 0;

    uint64_t batchesTotal = 0;
    uint64_t requestsPerBatchSum = 0;
    uint64_t requestsPerBatchCount = 0;

    uint64_t kvsItems = 0;
    uint64_t kvsBytesUsed = 0;
    uint64_t writeQueueDepth = 0;
    uint64_t readBufferUsageBytes = 0;
    uint64_t inFlightRequests = 0;
    uint64_t epollWaitTotal = 0;
    uint64_t epollEventsTotal = 0;
    uint64_t syscallRecvTotal = 0;
    uint64_t syscallSendTotal = 0;
    uint64_t syscallAcceptTotal = 0;

    uint64_t hotKeySamplesDropped = 0;
    std::vector<std::pair<uint64_t, uint64_t>> hotKeysTop;
    static constexpr std::size_t BatchHistogramBucketCount = 8;
    uint64_t batchHistogram[BatchHistogramBucketCount] = {0};
};

struct ShardInfo {
    std::string workerIndex = "0";
    std::string cpu = "-1";
    std::string numaNode = "-1";
    std::string nicQueueId = "-1";
};

class MetricsCollector {
  public:
    MetricsCollector(std::string shardLabel, std::string nodeLabel, ShardInfo shardInfo = {}, bool hotKeySamplerEnabled = false, std::size_t hotKeyTopN = 8);

    void incrementRequest(RequestOperation op) noexcept;
    void incrementResponse(ResponseStatus status) noexcept;
    void addBytesRx(std::size_t amount) noexcept;
    void addBytesTx(std::size_t amount) noexcept;
    void connectionAccepted() noexcept;
    void connectionClosed(CloseReason reason) noexcept;
    void recordBatch(std::size_t requestsInBatch) noexcept;
    void setWriteQueueDepth(std::size_t depth) noexcept;
    void setReadBufferUsageBytes(std::size_t bytes) noexcept;
    void setKvsState(std::size_t items, std::size_t bytesUsed) noexcept;
    void setInFlightRequests(std::size_t inFlight) noexcept;
    void incrementEpollWait(std::size_t eventCount) noexcept;
    void incrementSyscallRecv() noexcept;
    void incrementSyscallSend() noexcept;
    void incrementSyscallAccept() noexcept;
    void sampleKeyHash(uint64_t keyHash) noexcept;

    MetricsSnapshot snapshot() const noexcept;
    std::string renderPrometheus() const;
    std::string renderShardInfoJson() const;

  private:
    mutable std::mutex mutex;
    MetricsSnapshot metrics;
    const std::string shard;
    const std::string node;
    const ShardInfo shardInfo;
    bool hotKeySamplerEnabled = false;
    std::size_t hotKeyTopN = 8;
};


} // namespace metrics
