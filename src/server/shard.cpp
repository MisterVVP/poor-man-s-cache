#include "shard.h"

using namespace server;

const char* ServerShard::processCommand(const Command& command)
{
    const char* response = nullptr;
    if (command.commandCode == 1) {
        auto setRes = keyValueStore->set(command.key.get(), command.value.get(), command.hash);
        response = setRes ? "OK" : "ERROR: Internal error";
    } else {
        response = "ERROR: Invalid command code";
    }

    return response;
}

const char* ServerShard::processQuery(const Query& query)
{
    const char* response = nullptr;

    if (query.queryCode == 1) {
        auto value = keyValueStore->get(query.key.get(), query.hash);
        response = value ? value : "(nil)";
    } else {
        response = "ERROR: Invalid query code";
    }

    return response;
}

server::Query::Query(QueryCode code, const char *arg_key, uint_fast64_t hash): queryCode(code), hash(hash)
{
    auto kSize = strlen(arg_key) + 1;
    key = std::make_unique<char[]>(kSize);
    memcpy(key.get(), arg_key, kSize);
    key.get()[kSize-1] = '\0';
}

server::Command::Command(CommandCode code, const char *arg_key, const char *arg_value, uint_fast64_t hash): commandCode(code), hash(hash)
{
    auto vSize = strlen(arg_value) + 1;
    value = std::make_unique<char[]>(vSize);
    memcpy(value.get(), arg_value, vSize);
    value.get()[vSize-1] = '\0';

    auto kSize = strlen(arg_key) + 1;
    key = std::make_unique<char[]>(kSize);
    memcpy(key.get(), arg_key, kSize);
    key.get()[kSize-1] = '\0';
}