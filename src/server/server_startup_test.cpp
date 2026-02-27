#include <gtest/gtest.h>

#include "server.hpp"

namespace {

TEST(CacheServerStartupChecksTest, PassesForValidServerInitialization) {
    server::ServerSettings settings{};
    settings.port = 0;
    settings.numShards = 2;

    auto metrics = std::make_shared<metrics::MetricsCollector>(
        "0",
        "test-node",
        metrics::ShardInfo{"0", "0", "0", "0"},
        false,
        8);

    server::CacheServer cacheServer{settings, metrics};

    std::string error;
    EXPECT_TRUE(cacheServer.runStartupSelfChecks(&error));
    EXPECT_TRUE(error.empty());
}

TEST(CacheServerStartupChecksTest, FailsWhenShardCountIsZero) {
    server::ServerSettings settings{};
    settings.port = 0;
    settings.numShards = 0;

    auto metrics = std::make_shared<metrics::MetricsCollector>(
        "0",
        "test-node",
        metrics::ShardInfo{"0", "0", "0", "0"},
        false,
        8);

    server::CacheServer cacheServer{settings, metrics};

    std::string error;
    EXPECT_FALSE(cacheServer.runStartupSelfChecks(&error));
    EXPECT_EQ(error, "startup self-check failed: number of shards must be greater than zero");
}

} // namespace
