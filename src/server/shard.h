#pragma once
#include <cstdint>
#include <cstddef>
#include <functional>
#include "../non_copyable.h"
#include "../kvs/kvs.h"
#include "conn_manager.hpp"
#include "constants.hpp"
#include "protocol.h"


namespace server {
    using namespace kvs;

    struct alignas(64) Command {
        CommandCode commandCode;
        std::unique_ptr<char[]> key;
        size_t kSize = 0;
        std::unique_ptr<char[]> value;
        size_t vSize = 0;
        uint_fast64_t hash;

        Command(CommandCode code, const char* arg_key, size_t keyLen,
                const char* arg_value, size_t valueLen,
                uint_fast64_t hash);
    };

    struct alignas(64) Query {
        QueryCode queryCode;
        std::unique_ptr<char[]> key;
        size_t kSize = 0;
        uint_fast64_t hash;

        Query(QueryCode code, const char* arg_key, size_t keyLen, uint_fast64_t hash);
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