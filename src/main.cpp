#include <thread>
#include <iostream>
#include <signal.h>
#include "metrics/metrics.hpp"
#include "server/server.hpp"
#include "env.hpp"

using namespace server;

int main(int argc, char* argv[]) {
    std::string cliListen;
    std::string metricsListen;
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::string_view(argv[i]) == "--listen") {
            cliListen = argv[++i];
            continue;
        }
        if (std::string_view(argv[i]) == "--metrics-listen") {
            metricsListen = argv[++i];
        }
    }
    auto serverPort = cliListen.empty() ? getFromEnv<int>("CACHE_PORT", true) : std::stoi(cliListen);
    auto numShards = getFromEnv<uint_fast32_t>("NUM_SHARDS", false, 24);
    auto sockBufferSize = getFromEnv<int>("SOCK_BUF_SIZE", false, 1048576);
    auto connQueueLimit = getFromEnv<uint_fast32_t>("CONN_QUEUE_LIMIT", false, 1048576);
    auto enableCompression = getFromEnv<bool>("ENABLE_COMPRESSION", false, true);
    auto respInlineCapacity = getFromEnv<std::size_t>("RESP_INLINE_CAPACITY", false, static_cast<std::size_t>(255));

    auto metricsHost = std::string{getFromEnv<const char*>("METRICS_HOST", false, "0.0.0.0")};
    auto metricsPort = getFromEnv<int>("METRICS_PORT", false, 9100);
    auto metricsPortOffset = getFromEnv<int>("METRICS_PORT_OFFSET", false, 0);
    auto shardLabel = std::string{getFromEnv<const char*>("PMC_SHARD", false, "0")};
    auto nodeLabel = std::string{getFromEnv<const char*>("PMC_NODE", false, "local")};

    if (!metricsListen.empty()) {
        auto colonPos = metricsListen.rfind(':');
        if (colonPos != std::string::npos) {
            std::string hostPart = metricsListen.substr(0, colonPos);
            std::string portPart = metricsListen.substr(colonPos + 1);
            if (!hostPart.empty()) {
                metricsHost = hostPart;
            }
            metricsPort = std::stoi(portPart);
        } else {
            metricsPort = std::stoi(metricsListen);
        }
    }

    metricsPort += metricsPortOffset;

    metrics::MetricsConfig metricsConfig{metricsHost, metricsPort, shardLabel, nodeLabel};
    auto metricsCollector = std::make_shared<metrics::MetricsCollector>(shardLabel, nodeLabel);
    metrics::MetricsHttpServer metricsServer{metricsConfig, *metricsCollector};

    ServerSettings serverSettings { serverPort, numShards, sockBufferSize, connQueueLimit, enableCompression, respInlineCapacity };

    CacheServer cacheServer { serverSettings, metricsCollector };

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

    metricsServer.start();
    return cacheServer.Start();
}
