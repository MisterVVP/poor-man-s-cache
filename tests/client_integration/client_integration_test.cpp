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
    throw std::runtime_error("Unable to connect to cache cluster");
}

std::optional<ClusterConfig> clusterConfigFromEnv() {
    const char* clusterHostEnv = std::getenv("CACHE_HOST");
    const char* clusterPortEnv = std::getenv("CACHE_PORT");
    const char* clusterShardsEnv = std::getenv("CLUSTER_WORKERS");

    if (!clusterShardsEnv) {
        return std::nullopt;
    }

    ClusterConfig cfg;
    cfg.host = clusterHostEnv ? clusterHostEnv : "127.0.0.1";
    cfg.basePort = static_cast<std::uint16_t>(
        std::stoi(clusterPortEnv ? clusterPortEnv : "9001")
    );
    cfg.shardCount = static_cast<std::uint32_t>(std::stoul(clusterShardsEnv));

    if (cfg.shardCount == 0) {
        fail("CLUSTER_WORKERS must be greater than zero when provided");
    }

    return cfg;
}

std::pair<std::string, std::string>
selectDistinctShardKeys(pmc::ClusterCacheClient& cluster, const std::string& prefix) {
    for (int attempt = 0; attempt < 1000; ++attempt) {
        std::string keyA = prefix + "-a-" + std::to_string(attempt);
        std::string keyB = prefix + "-b-" + std::to_string(attempt);

        if (cluster.shardForKey(keyA) != cluster.shardForKey(keyB)) {
            return {keyA, keyB};
        }
    }
    fail("Unable to pick keys that map to distinct shards");
}

} // namespace

int main() {
    const char* hostEnv = std::getenv("CACHE_HOST");
    const char* portEnv = std::getenv("CACHE_PORT");

    pmc::CacheClient::Options options;
    options.host = hostEnv ? hostEnv : "127.0.0.1";
    if (portEnv) {
        options.port = static_cast<std::uint16_t>(std::stoi(portEnv));
    }

    pmc::CacheClient client(options);
    connectWithRetry(client);

    const std::string keyPrefix = "cpp-client-it-" + randomSuffix();
    const std::string key1 = keyPrefix + "-k1";
    const std::string key2 = keyPrefix + "-k2";
    const std::string value1 = "value-1";
    const std::string value2 = "value-2";

    // ---------------------------------------------------------
    // BASIC CRUD
    // ---------------------------------------------------------
    {
        auto missing = client.get(key1);
        expect(missing.notFound(), "Missing GET should return NotFound");

        auto s = client.set(key1, value1);
        expect(s.ok(), "SET should succeed");

        auto g = client.get(key1);
        expect(g.ok(), "GET after SET should succeed");
        expect(g.value == value1, "GET should return correct value");

        auto d = client.del(key1);
        expect(d.ok(), "DEL should succeed");

        auto g2 = client.get(key1);
        expect(g2.notFound(), "GET after DEL should return NotFound");
    }

    // ---------------------------------------------------------
    // PIPELINING TEST (single-instance)
    // ---------------------------------------------------------
    {
        const auto id1 = client.enqueueSet(key1, value1);
        const auto id2 = client.enqueueSet(key2, value2);
        client.flush();

        auto r1 = client.waitFor(id1);
        auto r2 = client.waitFor(id2);
        expect(r1.ok() && r2.ok(), "Pipelined SET must succeed");

        auto g1 = client.get(key1);
        expect(g1.ok() && g1.value == value1, "GET must return correct pipelined value");

        auto g2 = client.get(key2);
        expect(g2.ok() && g2.value == value2, "GET must return correct pipelined value");
    }

    // ---------------------------------------------------------
    // CLUSTER MODE TESTS
    // ---------------------------------------------------------
    if (auto clusterCfg = clusterConfigFromEnv()) {
        auto cluster = connectClusterWithRetry(*clusterCfg);

        auto [keyA, keyB] =
            selectDistinctShardKeys(cluster, keyPrefix + "-cluster");

        std::string valA = value1 + "-cluster";
        std::string valB = value2 + "-cluster";

        // -------------------
        // MULTI-SHARD CRUD
        // -------------------
        {
            auto mA = cluster.get(keyA);
            auto mB = cluster.get(keyB);
            expect(mA.notFound, "Fresh cluster GET(A) must be NotFound");
            expect(mB.notFound, "Fresh cluster GET(B) must be NotFound");

            auto sA = cluster.set(keyA, valA);
            auto sB = cluster.set(keyB, valB);
            expect(sA.ok, "Cluster SET(A) must succeed");
            expect(sB.ok, "Cluster SET(B) must succeed");

            auto gA = cluster.get(keyA);
            auto gB = cluster.get(keyB);
            expect(gA.ok, "Cluster GET(A) after SET must succeed");
            expect(gB.ok, "Cluster GET(B) after SET must succeed");

            expect(gA.value == valA, "Cluster GET(A) returned incorrect value");
            expect(gB.value == valB, "Cluster GET(B) returned incorrect value");

            auto dA = cluster.del(keyA);
            expect(dA.ok, "Cluster DEL(A) must succeed");

            auto gA2 = cluster.get(keyA);
            expect(gA2.notFound, "Deleted key(A) must be missing");

            auto gB2 = cluster.get(keyB);
            expect(gB2.ok, "Other shard(B) must stay intact");
        }

        // ---------------------------------------------------------
        // CLUSTER PIPELINING (simplified)
        // ---------------------------------------------------------
        {
            cluster.enqueueSet(keyA, valA);
            cluster.enqueueSet(keyB, valB);
            cluster.enqueueGet(keyA);
            cluster.enqueueGet(keyB);
            cluster.enqueueDelete(keyB);

            cluster.flushAll();

            // Verify final state with synchronous GETs
            auto gA = cluster.get(keyA);
            expect(gA.ok, "Cluster GET(A) after pipelined SET must succeed");
            expect(gA.hasValue(), "Cluster GET(A) must contain a value");
            expect(gA.value == valA, "Cluster GET(A) returned incorrect value");

            auto gB = cluster.get(keyB);
            expect(gB.notFound, "Cluster GET(B) must reflect pipelined DELETE");

            auto dB = cluster.del(keyB);
            expect(dB.notFound, "Cluster DEL(B) must return not found");

            auto gB2 = cluster.get(keyB);
            expect(gB2.notFound, "Cluster key(B) must be deleted");
        }
    }

    std::cout << "Client integration test completed successfully\n";
    return EXIT_SUCCESS;
}
