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
#include <mutex>
#include <shared_mutex>
#include <queue>
#include <fcntl.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include "kvs/kvs.h"

using namespace kvs;

#define EPOLL_WAIT_TIMEOUT -1
#define MAX_EVENTS 1024
#define READ_BUFFER_SIZE 1024

struct CacheServerMetrics {
    uint_fast64_t serverNumErrors = 0;
    uint_fast32_t serverNumActiveConnections = 0;
    uint_fast64_t storageNumEntries = 0;
    uint_fast32_t storageNumResizes = 0;  
    uint_fast64_t serverNumRequests = 0;
};

class CacheServer {
    private:
        static constexpr uint_fast16_t METRICS_UPDATE_FREQUENCY_SEC = 5;
        static constexpr uint_fast16_t WORKER_THREAD_COUNT = 4;
        static constexpr uint_fast16_t CONN_QUEUE_LIMIT = 65535;
        static constexpr uint_fast16_t ACTIVE_CONN_LIMIT = 65000;

        struct Command {
            int commandCode; // 0: Reserved, 1: SET
            char* key = nullptr;
            char* value = nullptr;
            int client_fd;
        };

        struct Query {
            int queryCode;  // 0: Reserved, 1: GET
            char* key= nullptr;
            int client_fd;
        };

        std::atomic<uint_fast32_t> activeConnections = 0;
        std::atomic<uint_fast64_t> numErrors = 0;
        std::atomic<uint_fast64_t> numRequests = 0;
        std::atomic<bool>& cancellationToken;
        std::vector<std::jthread> workerThreads;
        std::jthread metricsUpdaterThread;
        std::vector<int> epollInstances;
        std::mutex epollMutex;
        mutable std::shared_mutex kvsMutex;
        size_t nextThread = 0;
        KeyValueStore keyValueStore;
        std::atomic<bool> isRunning;
        int port = 9001;
        int server_fd;
        int_fast8_t processCommand(const Command& command);
        int_fast8_t processQuery(const Query& query);

        void workerLoop(int epoll_fd, std::stop_token stopToken);
        void distributeConnection(int client_fd);
        void closeConnection(int client_fd);
        void metricsUpdater(std::queue<CacheServerMetrics>& channel, std::stop_token stopToken);
        void sendMetricsUpdates(std::queue<CacheServerMetrics>& channel);

    public:
        CacheServer(int port, std::atomic<bool>& cToken);
        ~CacheServer();
        int Start(std::queue<CacheServerMetrics>& channel);
        void Stop();
};