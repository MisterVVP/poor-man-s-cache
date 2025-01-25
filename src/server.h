#pragma once
#include <atomic>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include "kvs/kvs.h"

#define MAX_EVENTS 24
#define READ_BUFFER_SIZE 1024

using namespace kvs;

struct CacheServerMetrics {
    uint64_t serverNumErrors = 0;
    uint_fast64_t storageNumEntries = 0;
    uint_fast32_t storageNumResizes = 0;
};

class CacheServer {
    private:
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

        uint64_t numErrors;
        std::atomic<bool>& cancellationToken;
        std::shared_ptr<KeyValueStore> keyValueStore_ptr;
        int port = 9001;
        int_fast8_t processCommand(const Command& command);
        int_fast8_t processQuery(const Query& query);

    public:
        CacheServer(int port, std::shared_ptr<KeyValueStore> kvs_ptr, std::atomic<bool>& cToken);
        int Start(std::queue<CacheServerMetrics>& channel);
        void Stop();

        uint64_t getNumErrors() const {
            return numErrors;
        }
};