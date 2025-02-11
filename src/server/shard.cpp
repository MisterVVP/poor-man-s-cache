#include "shard.h"

using namespace server;

const char* ServerShard::processCommand(const Command &command)
{
    const char* response = nullptr;
    if (command.commandCode == 1) {
        auto setRes = keyValueStore.set(command.key, command.value, command.hash);
        response = setRes ? "OK" : "ERROR: Internal error";
    } else {
        response = "ERROR: Invalid command code";
    }

    return response;
}

const char* ServerShard::processQuery(const Query &query)
{
    const char* response = nullptr;

    if (query.queryCode == 1) {
        const char* value = keyValueStore.get(query.key, query.hash);
        response = value ? value : "(nil)";
    } else {
        response = "ERROR: Invalid query code";
    }

    return response;
}
