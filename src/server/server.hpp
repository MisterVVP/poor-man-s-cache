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
#include <vector>
#include <latch>
#include <queue>
#include <semaphore>
#include <string_view>
#include <fcntl.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include "../hash/hash.hpp"
#include "../non_copyable.hpp"
#include "sockutils.hpp"
#include "conn_manager.hpp"
#include "shard.hpp"
#include "protocol.hpp"
#include "constants.hpp"
#include "coroutines.hpp"

namespace server {

    using namespace kvs;

    struct CacheServerMetrics {
        uint_fast64_t numErrors = 0;
        uint_fast32_t numActiveConnections = 0;
        uint_fast64_t numRequests = 0;
        uint_fast32_t eventsPerBatch = 0;

        CacheServerMetrics(uint_fast64_t numErrors, uint_fast32_t numConnections, uint_fast64_t numRequests, uint_fast32_t eventsPerBatch):
        numErrors(numErrors), numActiveConnections(numConnections), numRequests(numRequests), eventsPerBatch(eventsPerBatch) {}
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
    };

    class CacheServer : NonCopyableOrMovable {
        private:
            struct RequestPart {
                size_t size;
                char* part;
                size_t location;

                RequestPart(char* part, size_t size, size_t location): part(part), size(size), location(location){}
            };

            std::latch shutdownLatch{2};
            std::binary_semaphore metricsSemaphore{0};
            std::unique_ptr<ConnManager> connManager;
            std::mutex req_handle_mutex;
            std::atomic<uint_fast64_t> numErrors = 0;
            std::atomic<uint_fast64_t> numRequests = 0;
            std::atomic<uint_fast32_t> eventsPerBatch = 0;
            std::atomic<bool> isRunning = false;
            std::jthread metricsUpdaterThread;
            std::jthread connManagerThread;
            std::jthread reqHandlerThread;

            uint_fast16_t numShards;
            std::vector<ServerShard> serverShards;
            int port;
            int server_fd;
            int epoll_fd;
            epoll_event epoll_events[MAX_EVENTS];

            AsyncReadTask readRequestAsync(int client_fd);
            ProcessRequestTask processRequest(const RequestView& request, int client_fd);
            ResponsePacket processRequestSync(const RequestView& request);
            HandleReqTask handleRequests();
            AsyncSendTask sendResponse(int client_fd, const ResponsePacket& response);
            void sendResponses(int client_fd, const std::vector<ResponsePacket>& responses);
            void metricsUpdater(std::queue<CacheServerMetrics>& channel, std::stop_token stopToken);
        public:
            CacheServer(const ServerSettings settings = ServerSettings{});
            ~CacheServer();

            /// @brief Starts processing incoming requests
            /// @param channel metrics queue to report to
            /// @return operation result, 0 - success, other values - failure
            int Start(std::queue<CacheServerMetrics>& channel);

            /// @brief Gracefully stops server, restart is not (yet) supported
            void Stop() noexcept;
    };
}
