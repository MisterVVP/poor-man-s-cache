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
#include "../trashcan/trashcan.hpp"
#include "sockutils.h"

namespace server {

    using namespace kvs;

    #define EPOLL_WAIT_TIMEOUT -1
    #define MAX_EVENTS 2048
    #define READ_BUFFER_SIZE 1024
    #define MSG_SEPARATOR 0x1F

    struct CacheServerMetrics {
        uint_fast64_t serverNumErrors = 0;
        uint_fast32_t serverNumActiveConnections = 0;
        uint_fast64_t serverNumRequests = 0;

        CacheServerMetrics(uint_fast64_t numErrors, uint_fast32_t numConnections, uint_fast64_t numRequests):
            serverNumErrors(numErrors), serverNumActiveConnections(numConnections), serverNumRequests(numRequests) {
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

    struct ServerShard {
        int_fast16_t shardId;
        KeyValueStore keyValueStore;

        int_fast8_t processCommand(const Command& command);
        int_fast8_t processQuery(const Query& query);
    };

    struct ServerSettings {
        int port = 9001; // server port
        uint_fast16_t numShards = 24; // number of server shards
        uint_fast16_t trashEmptyFrequency = 100;// number of requests to process between trash cleanup
    };

    class CacheServer : NonCopyable {
        private:
            struct RequestPart {
                size_t size;
                char* part;
                size_t location;

                RequestPart(char* pval, size_t psize, size_t ploc) {
                    part = pval;
                    size = psize;
                    location = ploc;
                }
            };

            static constexpr std::chrono::seconds METRICS_UPDATE_FREQUENCY_SEC = std::chrono::seconds(4);
            static constexpr uint16_t READ_NUM_RETRY_ON_INT = 3;
            static constexpr uint_fast16_t CONN_QUEUE_LIMIT = 2048; // depends on tcp_max_syn_backlog, ignored when tcp_syncookies = 1

            std::binary_semaphore metricsSemaphore{0};
            std::atomic<uint_fast64_t> numErrors = 0;
            std::atomic<uint_fast64_t> numRequests = 0;
            std::atomic<uint_fast32_t> activeConnectionsCounter = 0;
            std::atomic<bool>& cancellationToken;
            std::atomic<bool> isRunning = false;
            std::jthread metricsUpdaterThread;

            uint_fast16_t trashEmptyFrequency = 100;
            uint_fast16_t numShards = 24;
            std::unique_ptr<ServerShard[]> serverShards;
            int port = 9001;
            int server_fd;
            int epoll_fd;
            epoll_event epoll_events[MAX_EVENTS];
            Trashcan<char> trashcan;

            Command createCommand(uint_fast16_t code, char* key, char* value, uint_fast64_t hash, int client_fd) const;
            Query createQuery(uint_fast16_t code, char* key, uint_fast64_t hash, int client_fd) const;
            int_fast8_t handleRequest();
            void closeConnection(int client_fd, bool fullShutdown = false);
            void shutdownConnection(int client_fd, int how = SHUT_RDWR);
            void metricsUpdater(std::queue<CacheServerMetrics>& channel, std::stop_token stopToken);
        public:
            CacheServer(std::atomic<bool>& cToken, const ServerSettings settings = ServerSettings{});
            ~CacheServer();
            int Start( std::queue<CacheServerMetrics>& channel);
            void Stop() noexcept;
    };
}