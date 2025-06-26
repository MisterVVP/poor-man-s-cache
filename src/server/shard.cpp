#include "shard.h"

using namespace server;

const char* ServerShard::processCommand(const Command& command)
{
    bool opRes = false;
    switch (command.commandCode)
    {
        case CommandCode::SET:
            opRes = keyValueStore->set(command.key.get(), command.kSize,
                                      command.value.get(), command.vSize,
                                      command.hash);
            return opRes ? OK : INTERNAL_ERROR;

        case CommandCode::DEL:
            opRes = keyValueStore->del(command.key.get(), command.kSize, command.hash);
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
            value = keyValueStore->get(query.key.get(), query.kSize, query.hash);
            return value ? value : NOTHING;

        default:
            return INVALID_QUERY_CODE;
    }
}

server::Query::Query(QueryCode code, const char *arg_key, size_t keyLen, uint_fast64_t hash)
    : queryCode(code), kSize(keyLen), hash(hash)
{
    key = std::make_unique<char[]>(keyLen + 1);
    memcpy(key.get(), arg_key, keyLen);
    key[keyLen] = '\0';
}

server::Command::Command(CommandCode code, const char *arg_key, size_t keyLen,
                         const char *arg_value, size_t valueLen,
                         uint_fast64_t hash)
    : commandCode(code), kSize(keyLen), vSize(valueLen), hash(hash)
{
    if (arg_value) { // not every command has value, e.g. DEL key1
        value = std::make_unique<char[]>(valueLen + 1);
        memcpy(value.get(), arg_value, valueLen);
        value[valueLen] = '\0';
    }
    key = std::make_unique<char[]>(keyLen + 1);
    memcpy(key.get(), arg_key, keyLen);
    key[keyLen] = '\0';
}