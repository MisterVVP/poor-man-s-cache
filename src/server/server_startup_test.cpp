#include <gtest/gtest.h>

#define private public
#include "server.hpp"
#undef private

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


TEST(CacheServerWorkerStateTest, ReadyTransitionRequiresExpectedStartingState) {
    server::ServerSettings settings{};
    settings.port = 0;
    settings.numShards = 1;

    auto metrics = std::make_shared<metrics::MetricsCollector>(
        "0",
        "test-node",
        metrics::ShardInfo{"0", "0", "0", "0"},
        false,
        8);

    server::CacheServer cacheServer{settings, metrics};

    cacheServer.setWorkerState(server::WorkerReadinessState::STARTING);
    EXPECT_TRUE(cacheServer.transitionWorkerState(server::WorkerReadinessState::STARTING, server::WorkerReadinessState::READY));
    EXPECT_EQ(cacheServer.workerReadinessState(), server::WorkerReadinessState::READY);

    cacheServer.setWorkerState(server::WorkerReadinessState::DRAINING);
    EXPECT_FALSE(cacheServer.transitionWorkerState(server::WorkerReadinessState::STARTING, server::WorkerReadinessState::READY));
    EXPECT_EQ(cacheServer.workerReadinessState(), server::WorkerReadinessState::DRAINING);
}

} // namespace
