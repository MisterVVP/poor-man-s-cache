#include <thread>
#include <iostream>
#include <signal.h>
#include "metrics/metrics.h"
#include "server/server.h"
#include "env.hpp"

using namespace server;

int main() {
    std::stop_source sSource;
    std::stop_token sToken;
    std::atomic<bool> cancellationToken{false};

    static std::function<void(int)> signalHandler = [&cancellationToken, &sSource, &sToken](int signal) {
        if (signal == SIGINT || signal == SIGTERM) {
            cancellationToken = true;
            if (sSource.stop_possible()) {
                sSource.request_stop();
            }
        }
    };

    auto signalDispatcher = [] (int signal) {
        if (signalHandler) {
            signalHandler(signal);
        }
    };

    signal(SIGINT, signalDispatcher); 
    signal(SIGTERM, signalDispatcher);

    std::queue<CacheServerMetrics> serverChannel;

    auto metricsHost = getFromEnv<const char*>("METRICS_HOST", true);
    auto metricsPort = getFromEnv<int>("METRICS_PORT", true);
    auto metricsUrl = std::format("{}:{}", metricsHost, metricsPort);
    metrics::MetricsServer metricsServer { metricsUrl };


    auto serverPort = getFromEnv<int>("SERVER_PORT", true);
    auto numShards = getFromEnv<uint_fast16_t>("NUM_SHARDS", false, 24);
    auto sockBufferSize = getFromEnv<int>("SOCK_BUF_SIZE", false, 1048576);
    auto enableCompression = getFromEnv<bool>("ENABLE_COMPRESSION", false, true);

    ServerSettings serverSettings { serverPort, numShards, sockBufferSize, enableCompression };

    CacheServer cacheServer { cancellationToken, serverSettings };


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

    sSource = metricsUpdaterThread.get_stop_source();
    sToken = metricsUpdaterThread.get_stop_token();
    auto returnCode = cacheServer.Start(serverChannel);

    return returnCode;
}