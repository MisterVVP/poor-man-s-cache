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
#include "sockutils.h"

using namespace kvs;

#define EPOLL_WAIT_TIMEOUT -1
#define MAX_EVENTS 1024
#define READ_BUFFER_SIZE 8096

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

class CacheServer {
    private:
        static constexpr std::chrono::seconds METRICS_UPDATE_FREQUENCY_SEC = std::chrono::seconds(4);
        static constexpr std::chrono::seconds CONN_THROTTLE_DELAY_SEC = std::chrono::seconds(3);
        static constexpr uint_fast16_t CONN_QUEUE_LIMIT = 2048; // depends on tcp_max_syn_backlog, ignored when tcp_syncookies = 1
        std::binary_semaphore metricsSemaphore{0};
        std::atomic<uint_fast64_t> numErrors = 0;
        std::atomic<uint_fast64_t> numRequests = 0;
        std::atomic<bool>& cancellationToken;
        uint_fast16_t numShards = 0;

        std::atomic<uint_fast32_t> activeConnectionsCounter = 0;

        std::jthread metricsUpdaterThread;
        std::unique_ptr<ServerShard[]> serverShards;
        std::atomic<bool> isRunning;
        int port = 9001;
        int server_fd;
        int epoll_fd;
        Command createCommand(uint_fast16_t code, char* key, char* value, uint_fast64_t hash, int client_fd);
        Query createQuery(uint_fast16_t code, char* key, uint_fast64_t hash, int client_fd);
        int_fast8_t handleRequest();
        void closeConnection(int client_fd);
        void metricsUpdater(std::queue<CacheServerMetrics>& channel, std::stop_token stopToken);
    public:
        CacheServer(int port, uint_fast16_t numShards, std::atomic<bool>& cToken);
        ~CacheServer();
        int Start(std::queue<CacheServerMetrics>& channel);
        void Stop();
};