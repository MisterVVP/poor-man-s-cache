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
#include <shared_mutex>
#include <mutex>
#include <fcntl.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include "kvs/kvs.h"
#include "hash.h"

using namespace kvs;

#define EPOLL_WAIT_TIMEOUT -1
#define MAX_EVENTS 24
#define READ_BUFFER_SIZE 8096

struct CacheServerMetrics {
    uint_fast64_t serverNumErrors = 0;
    uint_fast32_t serverNumActiveConnections = 0;
    uint_fast64_t serverNumRequests = 0;
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
        static constexpr uint_fast16_t METRICS_UPDATE_FREQUENCY_SEC = 5;
        static constexpr uint_fast16_t CONN_QUEUE_LIMIT = 65535;
        static constexpr uint_fast16_t ACTIVE_CONN_LIMIT = 65000;
        std::shared_mutex epollMutex;
        std::mutex shardMutex;
        std::mutex metricsMutex;
        std::atomic<uint_fast32_t> activeConnections = 0;
        std::atomic<uint_fast64_t> numErrors = 0;
        std::atomic<uint_fast64_t> numRequests = 0;
        std::atomic<bool>& cancellationToken;
        std::vector<int> epollInstances;
        std::vector<std::jthread> workerThreads;
        std::atomic<uint_fast16_t> nextThread = 0;
        uint_fast16_t workerThreadCount = 0;

        std::jthread metricsUpdaterThread;
        std::unique_ptr<ServerShard[]> serverShards;
        std::atomic<bool> isRunning;
        int port = 9001;
        int server_fd;
        int epoll_fd;

        void workerLoop(int epoll_fd, std::stop_token stopToken);
        void distributeConnection(int client_fd);
        int acceptConnection();
        void closeConnection(int client_fd);
        void metricsUpdater(std::queue<CacheServerMetrics>& channel, std::stop_token stopToken);
    public:
        CacheServer(int port, uint_fast16_t threadCount, std::atomic<bool>& cToken);
        ~CacheServer();
        int Start(std::queue<CacheServerMetrics>& channel);
        void Stop();
};