#include "metrics/metrics.h"
#include "env.h"
#include "server.h"

int main() {
    std::atomic<bool> cancellationToken{false};

    static std::function<void(int)> signalHandler = [&cancellationToken](int signal) {
        if (signal == SIGINT || signal == SIGTERM) {
            cancellationToken = false;            
        }
    };

    auto signalDispatcher = [] (int signal) {
        if (signalHandler) {
            signalHandler(signal);
        }
    };

    signal(SIGINT, signalDispatcher); 
    signal(SIGTERM, signalDispatcher);

    auto keyValueStore_ptr = std::make_shared<KeyValueStore>();


    auto metrics_host = getStrFromEnv("METRICS_HOST", true);
    auto metrics_port = getIntFromEnv("METRICS_PORT", true);
    auto metrics_url = std::format("{}:{}", metrics_host, metrics_port);
    metrics::MetricsServer metricsServer(metrics_url, keyValueStore_ptr);


    auto server_port = getIntFromEnv("SERVER_PORT", true);
    CacheServer cacheServer(server_port, keyValueStore_ptr, cancellationToken);
    auto returnCode = cacheServer.Start();

    return returnCode;
}
