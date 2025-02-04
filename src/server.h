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
#include <unordered_map>
#include <unordered_set>
#include <semaphore>
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

struct ClientAddr {
    sockaddr_in client_address;
    socklen_t client_len = sizeof(client_address);
    int client_fd;

    bool operator==(const ClientAddr &other) const {
        return client_address.sin_port == other.client_address.sin_port &&
               client_address.sin_addr.s_addr == other.client_address.sin_addr.s_addr;
    }
};
namespace std {

template <>
    struct hash<::ClientAddr> {
        size_t operator()(::ClientAddr a) const {
            return a.client_address.sin_port ^ a.client_address.sin_addr.s_addr;
        }
    };    
}

class CacheServer {
    private:
        static constexpr std::chrono::seconds METRICS_UPDATE_FREQUENCY_SEC = std::chrono::seconds(4);
        static constexpr std::chrono::seconds CONN_THROTTLE_DELAY_SEC = std::chrono::seconds(3);
        static constexpr uint_fast16_t CONN_QUEUE_LIMIT = 65535;
        static constexpr uint_fast16_t ACTIVE_CONN_LIMIT = 7000;
        std::mutex shardMutex;
        std::binary_semaphore connSemaphore{0};
        std::binary_semaphore metricsSemaphore{0};
        std::atomic<uint_fast64_t> numErrors = 0;
        std::atomic<uint_fast64_t> numRequests = 0;
        std::atomic<bool>& cancellationToken;
        std::vector<int> epollInstances;
        std::vector<std::jthread> workerThreads;
        std::atomic<uint_fast16_t> nextThread = 0;
        uint_fast16_t workerThreadCount = 0;

        std::unordered_set<ClientAddr> activeConnections;
        std::atomic<uint_fast32_t> activeConnectionsCounter = 0;

        std::jthread metricsUpdaterThread;
        std::unique_ptr<ServerShard[]> serverShards;
        std::atomic<bool> isRunning;
        int port = 9001;
        int server_fd;
        int epoll_fd;

        void workerLoop(int epoll_fd, std::stop_token stopToken);
        void distributeConnection(int client_fd);
        void closeConnection(int client_fd);
        void metricsUpdater(std::queue<CacheServerMetrics>& channel, std::stop_token stopToken);
    public:
        CacheServer(int port, uint_fast16_t threadCount, std::atomic<bool>& cToken);
        ~CacheServer();
        int Start(std::queue<CacheServerMetrics>& channel);
        void Stop();
};