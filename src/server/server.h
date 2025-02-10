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
#include "../kvs/kvs.h"
#include "../hash/hash.h"
#include "../non_copyable.h"
#include "../utils/trashcan.hpp"
#include "sockutils.h"

namespace server {

    using namespace kvs;

    #define EPOLL_WAIT_TIMEOUT -1
    #define MAX_EVENTS 2048
    #define MAX_REQUEST_SIZE 536870912
    #define READ_BUFFER_SIZE 8192
    #define MSG_SEPARATOR 0x1F

    struct CacheServerMetrics {
        uint_fast64_t serverNumErrors = 0;
        uint_fast32_t serverNumActiveConnections = 0;
        uint_fast64_t serverNumRequests = 0;

        CacheServerMetrics(uint_fast64_t numErrors, uint_fast32_t numConnections, uint_fast64_t numRequests):
            serverNumErrors(numErrors), serverNumActiveConnections(numConnections), serverNumRequests(numRequests) {
        }
    };

    class ConnManager {
        private:
            std::atomic<uint_fast32_t>& connCounter;
        public:
            void closeConnection(int client_fd);
        
        ConnManager(std::atomic<uint_fast32_t>& connCounter): connCounter(connCounter) {

        }
    };

    struct Command {
        uint_fast16_t commandCode; // 0: Reserved, 1: SET
        char* key = nullptr;
        char* value = nullptr;
        uint_fast64_t hash;
        int client_fd;
    };

    struct Query {
        uint_fast16_t queryCode;  // 0: Reserved, 1: GET
        char* key = nullptr;
        uint_fast64_t hash;
        int client_fd;
    };

    struct ServerShard: NonCopyable {
        public:
            int_fast16_t shardId;
            KeyValueStore keyValueStore;
            std::shared_ptr<ConnManager> connManager;

            int_fast8_t processCommand(const Command& command);
            int_fast8_t processQuery(const Query& query);

        private:
            static constexpr uint_fast32_t ASYNC_RESPONSE_SIZE_THRESHOLD = 1048576;
            int_fast8_t sendResponse(int client_fd, const char* response, const size_t responseSize);
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

            /// @brief Metrics update frequency, decrease for more up-to-date metrics, increase to save server resources
            static constexpr std::chrono::seconds METRICS_UPDATE_FREQUENCY_SEC = std::chrono::seconds(4);

            /// @brief Number of retries on EINTR
            static constexpr uint_fast16_t READ_NUM_RETRY_ON_INT = 3;

            /// @brief Max attempts to read client data from socket, required to avoid endless loop
            static constexpr uint_fast32_t READ_MAX_ATTEMPTS = (MAX_REQUEST_SIZE / READ_BUFFER_SIZE) * 2;

            /// @brief Server socket backlog, depends on tcp_max_syn_backlog, ignored when tcp_syncookies = 1
            static constexpr uint_fast16_t CONN_QUEUE_LIMIT = 2048; 

            std::binary_semaphore metricsSemaphore{0};
            std::atomic<uint_fast64_t> numErrors = 0;
            std::atomic<uint_fast64_t> numRequests = 0;
            std::atomic<uint_fast32_t> activeConnectionsCounter = 0;
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
            int sockBuffer;
            epoll_event epoll_events[MAX_EVENTS];
            Trashcan<char> trashcan;

            Command createCommand(uint_fast16_t code, char* key, char* value, uint_fast64_t hash, int client_fd) const;
            Query createQuery(uint_fast16_t code, char* key, uint_fast64_t hash, int client_fd) const;

            size_t readRequest(int client_fd, std::vector<RequestPart>& requestParts, bool shouldCloseConn = true);
            int_fast8_t handleRequest();
            void metricsUpdater(std::queue<CacheServerMetrics>& channel, std::stop_token stopToken);
        public:
            CacheServer(std::atomic<bool>& cToken, const ServerSettings settings = ServerSettings{});
            ~CacheServer();
            int Start(std::queue<CacheServerMetrics>& channel);
            void Stop() noexcept;
    };
}
