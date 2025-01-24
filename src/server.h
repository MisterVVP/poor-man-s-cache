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
#include "kvs.h"

#define MAX_EVENTS 24

// Structures for parsed commands and queries
struct Command {
    int commandCode; // 0: Reserved, 1: SET
    char* key = nullptr;
    char* value = nullptr;
    int client_fd;    // Client file descriptor to send response
};

struct Query {
    int queryCode;  // 0: Reserved, 1: GET
    char* key= nullptr;
    int client_fd;  // Client file descriptor to send response
};

class CacheServer {
    private:
        std::atomic<bool>& cancellationToken;
        std::shared_ptr<KeyValueStore> keyValueStore_ptr;
        int port = 9001;
        int_fast8_t processCommand(const Command& command);
        int_fast8_t processQuery(const Query& query);

    public:
        CacheServer(int port, std::shared_ptr<KeyValueStore> kvs_ptr, std::atomic<bool>& cToken);
        int Start();
        void Stop();
};