#include <thread>
#include <iostream>
#include <signal.h>
#include "metrics/metrics.hpp"
#include "server/server.hpp"
#include "env.hpp"

using namespace server;

int main() {
    auto serverPort = getFromEnv<int>("SERVER_PORT", true);
    auto numShards = getFromEnv<uint_fast32_t>("NUM_SHARDS", false, 24);
    auto sockBufferSize = getFromEnv<int>("SOCK_BUF_SIZE", false, 1048576);
    auto connQueueLimit = getFromEnv<uint_fast32_t>("CONN_QUEUE_LIMIT", false, 1048576);
    auto enableCompression = getFromEnv<bool>("ENABLE_COMPRESSION", false, true);
    auto respInlineCapacity = getFromEnv<std::size_t>("RESP_INLINE_CAPACITY", false, static_cast<std::size_t>(255));

    ServerSettings serverSettings { serverPort, numShards, sockBufferSize, connQueueLimit, enableCompression, respInlineCapacity };

    CacheServer cacheServer { serverSettings };

    static std::function<void(int)> signalHandler = [&cacheServer](int signal) {
        if (signal == SIGINT || signal == SIGTERM) {
            cacheServer.Stop();
        }
    };

    auto signalDispatcher = [] (int signal) {
        if (signalHandler) {
            signalHandler(signal);
        }
    };

    signal(SIGINT, signalDispatcher); 
    signal(SIGTERM, signalDispatcher);

    return cacheServer.Start();
}