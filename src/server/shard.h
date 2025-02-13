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
        std::unique_ptr<char[]> key;
        std::unique_ptr<char[]> value;
        uint_fast64_t hash;
        int client_fd;

        Command(uint_fast16_t code, const char* arg_key, const char* arg_value, uint_fast64_t hash, int client_fd);
    };

    struct Query {
        uint_fast16_t queryCode;  // 0: Reserved, 1: GET
        std::unique_ptr<char[]> key;
        uint_fast64_t hash;
        int client_fd;

        Query(uint_fast16_t code, const char* arg_key, uint_fast64_t hash, int client_fd);
    };

    struct ServerShard {
        public:
            int_fast16_t shardId;
            std::unique_ptr<KeyValueStore> keyValueStore;
            
            ServerShard(int_fast16_t shardId, KeyValueStoreSettings kvsSettings): shardId(shardId)
            {
                keyValueStore = std::make_unique<KeyValueStore>(kvsSettings);
            };

            const char* processCommand(const Command& command);
            const char* processQuery(const Query& query);
    };
}