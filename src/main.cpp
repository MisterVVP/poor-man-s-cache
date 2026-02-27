#include <thread>
#include <iostream>
#include <cstdlib>
#include <signal.h>
#include <optional>
#include <memory>
#include <atomic>
#include <exception>
#include "metrics/metrics.hpp"
#include "http/http_server.hpp"
#include "server/server.hpp"
#include "env.hpp"

using namespace server;

int main(int argc, char* argv[]) {
    try {
    std::string cliListen;
    std::string metricsListen;
    std::optional<uint_fast32_t> cliDrainTimeoutMs;
    std::optional<uint_fast32_t> cliDrainMaxClosePerTick;
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::string_view(argv[i]) == "--listen") {
            cliListen = argv[++i];
            continue;
        }
        if (std::string_view(argv[i]) == "--metrics-listen") {
            metricsListen = argv[++i];
            continue;
        }
        if (std::string_view(argv[i]) == "--drain-timeout-ms") {
            cliDrainTimeoutMs = std::stoul(argv[++i]);
            continue;
        }
        if (std::string_view(argv[i]) == "--drain-max-conn-close-per-tick") {
            cliDrainMaxClosePerTick = std::stoul(argv[++i]);
        }
    }
    auto serverPort = cliListen.empty() ? getFromEnv<int>("CACHE_PORT", true) : std::stoi(cliListen);
    auto numShards = getFromEnv<uint_fast32_t>("NUM_SHARDS", false, 24);
    auto sockBufferSize = getFromEnv<int>("SOCK_BUF_SIZE", false, 1048576);
    auto connQueueLimit = getFromEnv<uint_fast32_t>("CONN_QUEUE_LIMIT", false, 1048576);
    auto enableCompression = getFromEnv<bool>("ENABLE_COMPRESSION", false, true);
    auto respInlineCapacity = getFromEnv<std::size_t>("RESP_INLINE_CAPACITY", false, static_cast<std::size_t>(255));
    auto drainTimeoutMs = getFromEnv<uint_fast32_t>("DRAIN_TIMEOUT_MS", false, 5000);
    auto drainMaxConnClosePerTick = getFromEnv<uint_fast32_t>("DRAIN_MAX_CONN_CLOSE_PER_TICK", false, 1024);
    if (cliDrainTimeoutMs.has_value()) {
        drainTimeoutMs = *cliDrainTimeoutMs;
    }
    if (cliDrainMaxClosePerTick.has_value()) {
        drainMaxConnClosePerTick = *cliDrainMaxClosePerTick;
    }

    auto metricsEnabled = getFromEnv<bool>("METRICS_ENABLED", false, true);
    auto metricsHost = std::string{getFromEnv<const char*>("METRICS_HOST", false, "0.0.0.0")};
    auto metricsPort = getFromEnv<int>("METRICS_PORT", false, 9100);
    auto metricsPortOffset = getFromEnv<int>("METRICS_PORT_OFFSET", false, 0);
    auto shardLabel = std::string{getFromEnv<const char*>("PMC_SHARD", false, "0")};
    auto nodeLabel = std::string{getFromEnv<const char*>("PMC_NODE", false, "local")};
    auto workerCpu = std::string{getFromEnv<const char*>("PMC_CPU", false, "-1")};
    auto workerNuma = std::string{getFromEnv<const char*>("PMC_NUMA", false, "-1")};
    auto nicQueueId = std::string{getFromEnv<const char*>("PMC_NIC_QUEUE_ID", false, "-1")};
    auto hotKeySamplerEnabled = getFromEnv<bool>("PMC_DEBUG_HOT_KEYS", false, false);
    auto hotKeyTopN = getFromEnv<std::size_t>("PMC_HOTKEY_TOP_N", false, static_cast<std::size_t>(8));

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

    http::HttpServerConfig httpServerConfig{metricsHost, metricsPort};
    metrics::ShardInfo shardInfo{shardLabel, workerCpu, workerNuma, nicQueueId};
    auto metricsCollector = std::make_shared<metrics::MetricsCollector>(shardLabel, nodeLabel, shardInfo, hotKeySamplerEnabled, hotKeyTopN);

    ServerSettings serverSettings { serverPort, numShards, sockBufferSize, connQueueLimit, enableCompression, respInlineCapacity, drainTimeoutMs, drainMaxConnClosePerTick };

    CacheServer cacheServer { serverSettings, metricsCollector };
    httpServerConfig.readinessProbe = [&cacheServer]() noexcept {
        return cacheServer.workerReadinessState() == WorkerReadinessState::READY;
    };

    std::string startupCheckError;
    if (!cacheServer.runStartupSelfChecks(&startupCheckError)) {
        std::cerr << startupCheckError << std::endl;
        return EXIT_FAILURE;
    }
    std::cout << "startup self-check: data port bind success and shard arenas allocated" << std::endl;

    std::unique_ptr<http::HttpServer> metricsServer;

    static volatile sig_atomic_t pendingShutdownSignal = 0;
    auto signalDispatcher = [](int signal) {
        if (signal == SIGINT || signal == SIGTERM) {
            pendingShutdownSignal = signal;
        }
    };

    signal(SIGINT, signalDispatcher);
    signal(SIGTERM, signalDispatcher);

    std::atomic<bool> shutdownWatcherRunning{true};
    std::thread shutdownWatcher([&cacheServer, &shutdownWatcherRunning]() {
        while (shutdownWatcherRunning.load(std::memory_order_acquire)) {
            const auto signal = pendingShutdownSignal;
            if (signal == SIGINT || signal == SIGTERM) {
                const auto shutdownReason = signal == SIGTERM
                    ? metrics::ShutdownReason::Sigterm
                    : metrics::ShutdownReason::Sigint;
                cacheServer.Stop(shutdownReason);
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });

    if (metricsEnabled) {
        metricsServer = std::make_unique<http::HttpServer>(httpServerConfig, *metricsCollector);
        metricsServer->start();
        std::cout << "startup self-check: metrics port bind success" << std::endl;
    }

    const int startResult = cacheServer.Start();
    shutdownWatcherRunning.store(false, std::memory_order_release);
    if (shutdownWatcher.joinable()) {
        shutdownWatcher.join();
    }

    return startResult;
    } catch (const std::exception& ex) {
        std::cerr << "fatal startup error: " << ex.what() << std::endl;
        return EXIT_FAILURE;
    }
}
