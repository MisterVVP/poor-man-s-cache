#include <thread>
#include <iostream>
#include <signal.h>
#include "metrics/metrics.hpp"
#include "server/server.hpp"
#include "env.hpp"

using namespace server;

int main() {
    std::queue<CacheServerMetrics> serverChannel;

    auto metricsHost = getFromEnv<const char*>("METRICS_HOST", true);
    auto metricsPort = getFromEnv<int>("METRICS_PORT", true);
    auto metricsUrl = std::format("{}:{}", metricsHost, metricsPort);
    metrics::MetricsServer metricsServer { metricsUrl };


    auto serverPort = getFromEnv<int>("SERVER_PORT", true);
    auto numShards = getFromEnv<uint_fast32_t>("NUM_SHARDS", false, 24);
    auto sockBufferSize = getFromEnv<int>("SOCK_BUF_SIZE", false, 1048576);
    auto connQueueLimit = getFromEnv<uint_fast32_t>("CONN_QUEUE_LIMIT", false, 1048576);
    auto enableCompression = getFromEnv<bool>("ENABLE_COMPRESSION", false, true);

    ServerSettings serverSettings { serverPort, numShards, sockBufferSize, connQueueLimit, enableCompression };

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

    auto metricsUpdaterThread = std::jthread(
        [&serverChannel, &metricsServer](std::stop_token stopToken)
        {
            std::cout << "Metrics updater thread is running!" << std::endl;
            while (!stopToken.stop_requested())
            {
                while (!serverChannel.empty()) {
                    auto serverMetrics = serverChannel.front();
                    metricsServer.UpdateMetrics(serverMetrics);
                    serverChannel.pop();
                }
                std::this_thread::sleep_for(std::chrono::seconds(2));
            }
            std::cout << "Exiting metrics updater thread..." << std::endl;
        }
    );

    return cacheServer.Start(serverChannel);
}