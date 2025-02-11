#pragma once
#include <atomic>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <functional>
#include <thread>
#include <vector>
#include <queue>
#include <future>
#include <semaphore>
#include <fcntl.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include "../hash/hash.h"
#include "../non_copyable.h"
#include "../utils/trashcan.hpp"
#include "sockutils.h"
#include "conn_manager.hpp"
#include "shard.h"
#include "constants.hpp"

namespace server {

    using namespace kvs;

    struct CacheServerMetrics {
        uint_fast64_t serverNumErrors = 0;
        uint_fast32_t serverNumActiveConnections = 0;
        uint_fast64_t serverNumRequests = 0;

        CacheServerMetrics(uint_fast64_t numErrors, uint_fast32_t numConnections, uint_fast64_t numRequests):
            serverNumErrors(numErrors), serverNumActiveConnections(numConnections), serverNumRequests(numRequests) {
        }
    };

    struct ServerSettings {
        /// @brief Server port
        int port = 9001;
        /// @brief Number of server shards, increase for stability and performance, decrease to save server resources
        uint_fast16_t numShards = 24;
        /// @brief Empty dynamic memory on every {trashEmptyFrequency} request
        uint_fast16_t trashEmptyFrequency = 100;
        /// @brief Requested buffer size for server socket
        int sockBuffer = 1048576;
    };

    class CacheServer : NonCopyable {
        private:
            struct RequestPart {
                size_t size;
                char* part;
                size_t location;

                RequestPart(char* part, size_t size, size_t location): part(part), size(size), location(location){}
            };

            std::binary_semaphore metricsSemaphore{0};
            std::atomic<uint_fast64_t> numErrors = 0;
            std::atomic<uint_fast64_t> numRequests = 0;
            std::atomic<bool>& cancellationToken;
            std::atomic<bool> isRunning = false;
            std::jthread metricsUpdaterThread;
            ConnManager connManager;
            
            uint_fast16_t trashEmptyFrequency;
            uint_fast16_t numShards;
            std::unique_ptr<ServerShard[]> serverShards;
            int port;
            int server_fd;
            int epoll_fd;
            epoll_event epoll_events[MAX_EVENTS];
            Trashcan<char> trashcan;

            Command createCommand(uint_fast16_t code, char* key, char* value, uint_fast64_t hash, int client_fd) const;
            Query createQuery(uint_fast16_t code, char* key, uint_fast64_t hash, int client_fd) const;

            size_t readRequest(int client_fd, std::vector<RequestPart>& requestParts);
            int_fast8_t handleRequest();
            int_fast8_t sendResponse(int client_fd, const char* response, const size_t responseSize);

            void metricsUpdater(std::queue<CacheServerMetrics>& channel, std::stop_token stopToken);
        public:
            CacheServer(std::atomic<bool>& cToken, const ServerSettings settings = ServerSettings{});
            ~CacheServer();

            /// @brief Starts processing incoming requests
            /// @param channel metrics queue to report to
            /// @return operation result, 0 - success, other values - failure
            int Start(std::queue<CacheServerMetrics>& channel);

            /// @brief Gracefully stops server, restart is not (yet) supported
            void Stop() noexcept;
    };
}
