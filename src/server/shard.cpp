#include "shard.hpp"

using namespace server;

const char* ServerShard::processCommand(const Command& command)
{
    bool opRes = false;
    switch (command.commandCode)
    {
        case CommandCode::SET:
            opRes = keyValueStore->set(command.key.get(), command.value.get(), command.hash);
            return opRes ? OK : INTERNAL_ERROR;

        case CommandCode::DEL:
            opRes = keyValueStore->del(command.key.get(), command.hash);
            return opRes ? OK : KEY_NOT_EXISTS;
        
        default:
            return INVALID_COMMAND_CODE;
    }
}

const char* ServerShard::processQuery(const Query& query)
{
    const char* value = nullptr;
    switch (query.queryCode)
    {
        case QueryCode::GET:
            value = keyValueStore->get(query.key.get(), query.hash);
            return value ? value : NOTHING;

        default:
            return INVALID_QUERY_CODE;
    }
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
    if (arg_value) { // not every command has value, e.g. DEL key1
        auto vSize = strlen(arg_value) + 1;
        value = std::make_unique<char[]>(vSize);
        memcpy(value.get(), arg_value, vSize);
        value.get()[vSize-1] = '\0';
    }
    auto kSize = strlen(arg_key) + 1;
    key = std::make_unique<char[]>(kSize);
    memcpy(key.get(), arg_key, kSize);
    key.get()[kSize-1] = '\0';
}