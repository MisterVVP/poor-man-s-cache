#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

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
};

struct MetricsConfig {
    std::string listenHost = "0.0.0.0";
    int listenPort = 9100;
    std::string shardLabel = "0";
    std::string nodeLabel = "local";
};

class MetricsCollector {
  public:
    MetricsCollector(std::string shardLabel, std::string nodeLabel);

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

    MetricsSnapshot snapshot() const noexcept;
    std::string renderPrometheus() const;

  private:
    mutable std::mutex mutex;
    MetricsSnapshot metrics;
    const std::string shard;
    const std::string node;
};

class MetricsHttpServer {
  public:
    MetricsHttpServer(MetricsConfig config, MetricsCollector& collector);
    ~MetricsHttpServer();

    void start();
    void stop();

  private:
    void serveLoop();
    void handleClient(int client_fd);

    MetricsConfig config;
    MetricsCollector& collector;
    std::optional<int> server_fd;
    std::thread worker;
    std::atomic<bool> running{false};
};

} // namespace metrics
