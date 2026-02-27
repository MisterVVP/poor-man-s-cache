#pragma once
#include <atomic>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <mutex>
#include <functional>
#include <thread>
#include <chrono>
#include <vector>
#include <queue>
#include <string>
#include <string_view>
#include <fcntl.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include "../hash/hash.hpp"
#include "../non_copyable.hpp"
#include "../metrics/metrics.hpp"
#include "sockutils.hpp"
#include "conn_manager.hpp"
#include "shard.hpp"
#include "protocol.hpp"
#include "constants.hpp"
#include "coroutines.hpp"

namespace server {

    enum class WorkerReadinessState : uint8_t {
        STARTING = 0,
        READY = 1,
        DRAINING = 2,
        STOPPED = 3,
    };

    using namespace kvs;

    struct CacheServerMetrics {
        uint_fast64_t numErrors = 0;
        uint_fast32_t numActiveConnections = 0;
        uint_fast64_t numRequests = 0;

        CacheServerMetrics() = default;

        CacheServerMetrics(uint_fast64_t numErrors, uint_fast32_t numConnections, uint_fast64_t numRequests):
            numErrors(numErrors), numActiveConnections(numConnections), numRequests(numRequests) {}

    };

    class MetricsChannel {
        public:
            void push(const CacheServerMetrics& metrics) {
                std::scoped_lock lock(mutex);
                queue.push(metrics);
            }

            bool try_pop(CacheServerMetrics& metrics) {
                std::scoped_lock lock(mutex);
                if (queue.empty()) {
                    return false;
                }
                metrics = queue.front();
                queue.pop();
                return true;
            }

            bool empty() const {
                std::scoped_lock lock(mutex);
                return queue.empty();
            }

        private:
            mutable std::mutex mutex;
            std::queue<CacheServerMetrics> queue;
    };

    struct ServerSettings {

        /// @brief Server port
        int port = 9001;

        /// @brief Number of server shards, increase for stability and performance, decrease to save server resources
        uint_fast32_t numShards = 24;

        /// @brief Requested buffer size for server socket
        int sockBuffer = 1048576;

        /// @brief Server socket backlog, depends on tcp_max_syn_backlog, ignored when tcp_syncookies = 1, this is OS dependent, by default we are trying to pre-configure our server to have at least 1048576.
        /// Check net.core.netdev_max_backlog = 1048576, net.core.somaxconn = 1048576 settings of kernel
        uint_fast32_t connQueueLimit = 1048576;

        /// @brief Enable compression of stored values. Disable if RPS and processing speed is more important than memory consumption
        bool enableCompression = false;

        /// @brief Inline RESP response capacity before falling back to heap allocations
        std::size_t respInlineCapacity = 255;

        /// @brief Maximum drain duration before force-closing remaining connections
        uint_fast32_t drainTimeoutMs = 5000;

        /// @brief Max number of connections to force-close per drain tick
        uint_fast32_t drainMaxConnClosePerTick = 1024;
    };

    class CacheServer : NonCopyableOrMovable {
        private:
            struct RequestPart {
                size_t size;
                char* part;
                size_t location;

                RequestPart(char* part, size_t size, size_t location): part(part), size(size), location(location){}
            };

            std::unique_ptr<ConnManager> connManager;
            std::mutex req_handle_mutex;
            std::atomic<uint_fast64_t> numErrors = 0;
            std::atomic<uint_fast64_t> numRequests = 0;
            std::atomic<bool> isRunning = false;
            std::atomic<uint8_t> workerState{static_cast<uint8_t>(WorkerReadinessState::STARTING)};
            std::shared_ptr<metrics::MetricsCollector> metrics;
            std::size_t bufferedReadBytes = 0;

            uint_fast16_t numShards;
            std::vector<ServerShard> serverShards;
            int port;
            std::atomic<int> server_fd{-1};
            int epoll_fd;
            std::chrono::milliseconds drainTimeout{5000};
            uint_fast32_t drainMaxConnClosePerTick = 1024;
            epoll_event epoll_events[MAX_EVENTS];

            AsyncReadTask readRequestAsync(int client_fd);
            ResponsePacket processRequestSync(const RequestView& request, ConnectionData& connData);
            HandleReqTask handleRequests();
            void sendResponses(int client_fd, const std::vector<ResponsePacket>& responses);
            void updateKvsMetrics();
            void setWorkerState(WorkerReadinessState next) noexcept;
            void disableAccepting() noexcept;
        public:
            CacheServer(const ServerSettings settings = ServerSettings{}, std::shared_ptr<metrics::MetricsCollector> metrics = nullptr);
            ~CacheServer();

            /// @brief Starts processing incoming requests
            /// @return operation result, 0 - success, other values - failure
            int Start();

            bool runStartupSelfChecks(std::string* error = nullptr) const noexcept;

            WorkerReadinessState workerReadinessState() const noexcept;

            /// @brief Gracefully stops server, restart is not (yet) supported
            void Stop(metrics::ShutdownReason reason = metrics::ShutdownReason::Other) noexcept;
    };
}
