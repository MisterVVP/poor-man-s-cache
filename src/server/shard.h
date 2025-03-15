#pragma once
#include <cstdint>
#include <functional>
#include "../non_copyable.h"
#include "../kvs/kvs.h"
#include "conn_manager.hpp"
#include "constants.hpp"


namespace server {
    using namespace kvs;

    /// @brief Query codes, 0: Reserved, 1: GET
    enum QueryCode : uint_fast8_t {
        UnknownQuery = 0,
        GET = 1,
    };

    /// @brief Command codes, 0: Reserved, 1: SET
    enum CommandCode : uint_fast8_t {
        UnknownCommand = 0,
        SET = 1,
    };

     struct alignas(64) Command {
        CommandCode commandCode;
        std::unique_ptr<char[]> key;
        std::unique_ptr<char[]> value;
        uint_fast64_t hash;

        Command(CommandCode code, const char* arg_key, const char* arg_value, uint_fast64_t hash);
    };

    struct alignas(64) Query {
        QueryCode queryCode;
        std::unique_ptr<char[]> key;
        uint_fast64_t hash;

        Query(QueryCode code, const char* arg_key, uint_fast64_t hash);
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