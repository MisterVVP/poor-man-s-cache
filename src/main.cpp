#include <thread>
#include <iostream>
#include <signal.h>
#include "metrics/metrics.h"
#include "env.h"
#include "server.h"


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
    auto keyValueStore_ptr = std::make_shared<KeyValueStore>();


    auto metrics_host = getStrFromEnv("METRICS_HOST", true);
    auto metrics_port = getIntFromEnv("METRICS_PORT", true);
    auto metrics_url = std::format("{}:{}", metrics_host, metrics_port);
    metrics::MetricsServer metricsServer(metrics_url);


    auto server_port = getIntFromEnv("SERVER_PORT", true);
    CacheServer cacheServer(server_port, keyValueStore_ptr, cancellationToken);


    auto metricsUpdaterThread = std::jthread(
        [&serverChannel, &metricsServer](std::stop_token stopToken)
        {
            std::cout << "metrics updater thread is running!" << std::endl;
            while (!stopToken.stop_requested())
            {
                while (!serverChannel.empty()) {
                    auto serverMetrics = serverChannel.front();
                    metricsServer.UpdateMetrics(serverMetrics);
                    serverChannel.pop();
                }
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
            std::cout << "Exiting metrics updater thread..." << std::endl;
        }
    );

    sSource = metricsUpdaterThread.get_stop_source();
    sToken = metricsUpdaterThread.get_stop_token();
    auto returnCode = cacheServer.Start(serverChannel);

    return returnCode;
}