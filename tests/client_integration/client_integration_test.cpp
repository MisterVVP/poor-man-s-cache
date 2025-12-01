#include "client/cache_client.hpp"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <utility>

namespace {

struct ClusterConfig {
    std::string host{ "127.0.0.1" };
    std::uint16_t basePort{ 9001 };
    std::uint32_t shardCount{ 0 };
};

[[noreturn]] void fail(const std::string& message) {
    std::cerr << "[client-integration] " << message << std::endl;
    std::exit(EXIT_FAILURE);
}

void expect(bool condition, const std::string& message) {
    if (!condition) {
        fail(message);
    }
}

std::string randomSuffix() {
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<std::uint64_t> dist;
    std::ostringstream oss;
    oss << std::hex << dist(gen);
    return oss.str();
}

void connectWithRetry(pmc::CacheClient& client, int maxAttempts = 20) {
    using namespace std::chrono_literals;
    for (int attempt = 1; attempt <= maxAttempts; ++attempt) {
        try {
            client.connect();
            return;
        } catch (const std::exception& ex) {
            if (attempt == maxAttempts) {
                std::ostringstream oss;
                oss << "Failed to connect to cache server after " << maxAttempts
                    << " attempts: " << ex.what();
                fail(oss.str());
            }
            std::this_thread::sleep_for(250ms);
        }
    }
}

pmc::ClusterCacheClient connectClusterWithRetry(const ClusterConfig& config, int maxAttempts = 20) {
    using namespace std::chrono_literals;
    for (int attempt = 1; attempt <= maxAttempts; ++attempt) {
        try {
            pmc::ClusterCacheClient::ClusterOptions opts;
            opts.host = config.host;
            opts.basePort = config.basePort;
            opts.shardCount = config.shardCount;
            return pmc::ClusterCacheClient(std::move(opts));
        } catch (const std::exception& ex) {
            if (attempt == maxAttempts) {
                std::ostringstream oss;
                oss << "Failed to connect to cache cluster after " << maxAttempts
                    << " attempts: " << ex.what();
                fail(oss.str());
            }
            std::this_thread::sleep_for(250ms);
        }
    }

    // Unreachable but placates compiler warnings on some toolchains.
    throw std::runtime_error("Unable to connect to cache cluster");
}

std::optional<ClusterConfig> clusterConfigFromEnv() {
    const char* clusterHostEnv = std::getenv("PMC_CLUSTER_HOST");
    const char* clusterPortEnv = std::getenv("PMC_CLUSTER_BASE_PORT");
    const char* clusterShardsEnv = std::getenv("PMC_CLUSTER_SHARD_COUNT");

    if (clusterShardsEnv == nullptr) {
        return std::nullopt;
    }

    ClusterConfig cfg;
    cfg.host = clusterHostEnv ? clusterHostEnv : "127.0.0.1";
    cfg.basePort = static_cast<std::uint16_t>(std::stoi(clusterPortEnv ? clusterPortEnv : "9001"));
    cfg.shardCount = static_cast<std::uint32_t>(std::stoul(clusterShardsEnv));

    if (cfg.shardCount == 0) {
        fail("PMC_CLUSTER_SHARD_COUNT must be greater than zero when provided");
    }

    return cfg;
}

std::pair<std::string, std::string> selectDistinctShardKeys(pmc::ClusterCacheClient& cluster, const std::string& prefix) {
    for (int attempt = 0; attempt < 1000; ++attempt) {
        const std::string keyA = prefix + "-a-" + std::to_string(attempt);
        const std::string keyB = prefix + "-b-" + std::to_string(attempt);

        if (cluster.shardForKey(keyA) != cluster.shardForKey(keyB)) {
            return {keyA, keyB};
        }
    }

    fail("Unable to pick keys that map to distinct shards after multiple attempts");
}

} // namespace

int main() {
    const char* hostEnv = std::getenv("CACHE_HOST");
    const char* portEnv = std::getenv("CACHE_PORT");

    pmc::CacheClient::Options options;
    options.host = hostEnv ? hostEnv : "127.0.0.1";
    if (portEnv != nullptr) {
        options.port = static_cast<std::uint16_t>(std::stoi(portEnv));
    }

    pmc::CacheClient client(options);
    connectWithRetry(client);

    const std::string keyPrefix = std::string("cpp-client-it-") + randomSuffix();
    const std::string key1 = keyPrefix + "-k1";
    const std::string key2 = keyPrefix + "-k2";
    const std::string value1 = "value-1";
    const std::string value2 = "value-2";

    // Basic CRUD semantics.
    {
        auto getMissing = client.get(key1);
        expect(getMissing.notFound(), "Expected missing key to return NotFound");

        auto setResponse = client.set(key1, value1);
        expect(setResponse.ok(), "SET should return OK result");

        auto getResponse = client.get(key1);
        expect(getResponse.ok(), "GET after SET should succeed");
        expect(getResponse.value == value1, "GET should return the stored value");

        auto delResponse = client.del(key1);
        expect(delResponse.ok(), "DEL should return OK for existing key");

        auto getDeleted = client.get(key1);
        expect(getDeleted.notFound(), "GET after DEL should return NotFound");
    }

    // Verify pipelining helpers and response bookkeeping work correctly.
    {
        // Stage 1: pipeline writes together and confirm acknowledgements land in
        // order before issuing dependent reads.
        const auto setFooId = client.enqueueSet(key1, value1);
        const auto setBarId = client.enqueueSet(key2, value2);
        expect(client.pendingRequestCount() == 2, "SET commands should be pending before flush");
        client.flush();

        auto setFoo = client.waitFor(setFooId);
        auto setBar = client.waitFor(setBarId);
        expect(setFoo.ok() && setBar.ok(), "Pipelined SET operations should succeed");

        // Stage 2: queue reads and delete together to validate bookkeeping on a
        // clean pipeline and avoid cross-talk with earlier write responses.
        const auto getFooId = client.enqueueGet(key1);
        const auto getBarId = client.enqueueGet(key2);
        const auto delFooId = client.enqueueDelete(key1);
        expect(client.pendingRequestCount() == 3, "GET/DEL commands should be pending before flush");
        client.flush();

        auto getFoo = client.waitFor(getFooId);
        auto getBar = client.waitFor(getBarId);
        expect(getFoo.ok(), "GET response should be OK");
        expect(getFoo.value == value1, "GET response should contain latest value");
        expect(getBar.ok() && getBar.value == value2, "GET for second key should return stored value");

        auto delFoo = client.waitFor(delFooId);
        expect(delFoo.ok(), "DEL should return OK for existing key");

        auto finalGet = client.get(key1);
        expect(finalGet.notFound(), "Key should be missing after deletion");
    }

    if (auto clusterCfg = clusterConfigFromEnv()) {
        auto clusterClient = connectClusterWithRetry(*clusterCfg);

        const auto [clusterKeyA, clusterKeyB] = selectDistinctShardKeys(clusterClient, keyPrefix + "-cluster");
        const std::string clusterValA = value1 + "-cluster";
        const std::string clusterValB = value2 + "-cluster";

        // CRUD across multiple shards.
        {
            auto missingA = clusterClient.get(clusterKeyA);
            auto missingB = clusterClient.get(clusterKeyB);
            expect(missingA.notFound() && missingB.notFound(), "Missing cluster keys should return NotFound");

            auto setA = clusterClient.set(clusterKeyA, clusterValA);
            auto setB = clusterClient.set(clusterKeyB, clusterValB);
            expect(setA.ok() && setB.ok(), "Cluster SET should succeed on multiple shards");

            auto getA = clusterClient.get(clusterKeyA);
            auto getB = clusterClient.get(clusterKeyB);
            expect(getA.ok() && getB.ok(), "Cluster GET should succeed after SET");
            expect(getA.value == clusterValA && getB.value == clusterValB, "Cluster GET should return stored values");

            auto delA = clusterClient.del(clusterKeyA);
            expect(delA.ok(), "Cluster DEL should succeed");
            expect(clusterClient.get(clusterKeyA).notFound(), "Deleted cluster key should be missing");
            expect(clusterClient.get(clusterKeyB).ok(), "Second shard should remain intact");
        }

        // Cross-shard pipelining helpers.
        {
            const auto setAid = clusterClient.enqueueSet(clusterKeyA, clusterValA);
            const auto getAid = clusterClient.enqueueGet(clusterKeyA);
            const auto setBid = clusterClient.enqueueSet(clusterKeyB, clusterValB);
            const auto getBid = clusterClient.enqueueGet(clusterKeyB);
            const auto delBid = clusterClient.enqueueDelete(clusterKeyB);

            clusterClient.flushAll();

            auto getA = clusterClient.waitFor(clusterKeyA, getAid);
            expect(getA.ok(), "Cluster GET should be OK after pipelined flush");
            expect(getA.value == clusterValA, "Cluster GET should yield pipelined value");

            auto setA = clusterClient.waitFor(clusterKeyA, setAid);
            auto setB = clusterClient.waitFor(clusterKeyB, setBid);
            expect(setA.ok() && setB.ok(), "Pipelined SET operations should succeed");

            auto getB = clusterClient.waitFor(clusterKeyB, getBid);
            expect(getB.ok() && getB.value == clusterValB, "Second shard pipelined GET should return value");

            auto delB = clusterClient.waitFor(clusterKeyB, delBid);
            expect(delB.ok(), "Cluster DEL should be OK via pipelined requests");
            expect(clusterClient.get(clusterKeyB).notFound(), "Cluster key should be missing after pipelined delete");
        }
    }

    std::cout << "Client integration test completed successfully\n";
    return EXIT_SUCCESS;
}
