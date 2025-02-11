#pragma once
#include <cstdint>
#include <functional>
#include "../non_copyable.h"
#include "../kvs/kvs.h"
#include "conn_manager.hpp"
#include "constants.hpp"


namespace server {
    using namespace kvs;

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

            const char* processCommand(const Command& command);
            const char* processQuery(const Query& query);
    };
}