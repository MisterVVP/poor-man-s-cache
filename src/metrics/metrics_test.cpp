#include <gtest/gtest.h>

#include <string>

#include "metrics.hpp"

namespace {

TEST(MetricsCollectorTest, RenderPrometheusIncludesPhase5ObservabilityMetrics) {
    metrics::ShardInfo shardInfo{"2", "4", "1", "4"};
    metrics::MetricsCollector collector("2", "node-a", shardInfo, true, 2);

    collector.incrementRequest(metrics::RequestOperation::Get);
    collector.incrementRequest(metrics::RequestOperation::Set);
    collector.incrementRequest(metrics::RequestOperation::Del);
    collector.incrementRequest(metrics::RequestOperation::Other);

    collector.incrementResponse(metrics::ResponseStatus::Ok);
    collector.incrementResponse(metrics::ResponseStatus::NotFound);
    collector.incrementResponse(metrics::ResponseStatus::Error);

    collector.addBytesRx(128);
    collector.addBytesTx(64);
    collector.connectionAccepted();
    collector.recordBatch(3);
    collector.recordBatch(120);
    collector.recordBatch(500);
    collector.setWriteQueueDepth(11);
    collector.setReadBufferUsageBytes(22);
    collector.setKvsState(4, 321);
    collector.setInFlightRequests(7);
    collector.incrementEpollWait(3);
    collector.incrementSyscallAccept();
    collector.incrementSyscallRecv();
    collector.incrementSyscallSend();
    collector.sampleKeyHash(42);
    collector.sampleKeyHash(42);

    const std::string rendered = collector.renderPrometheus();

    EXPECT_NE(rendered.find("pmc_requests_total{op=\"get\",shard=\"2\",node=\"node-a\"} 1"), std::string::npos);
    EXPECT_NE(rendered.find("pmc_in_flight_requests{shard=\"2\",node=\"node-a\"} 7"), std::string::npos);
    EXPECT_NE(rendered.find("pmc_epoll_wait_total{shard=\"2\",node=\"node-a\"} 1"), std::string::npos);
    EXPECT_NE(rendered.find("pmc_epoll_events_total{shard=\"2\",node=\"node-a\"} 3"), std::string::npos);
    EXPECT_NE(rendered.find("pmc_syscall_accept_total{shard=\"2\",node=\"node-a\"} 1"), std::string::npos);
    EXPECT_NE(rendered.find("pmc_syscall_recv_total{shard=\"2\",node=\"node-a\"} 1"), std::string::npos);
    EXPECT_NE(rendered.find("pmc_syscall_send_total{shard=\"2\",node=\"node-a\"} 1"), std::string::npos);
    EXPECT_NE(rendered.find("pmc_batch_size_bucket{le=\"4\",shard=\"2\",node=\"node-a\"} 1"), std::string::npos);
    EXPECT_NE(rendered.find("pmc_batch_size_bucket{le=\"128\",shard=\"2\",node=\"node-a\"} 2"), std::string::npos);
    EXPECT_NE(rendered.find("pmc_batch_size_bucket{le=\"+Inf\",shard=\"2\",node=\"node-a\"} 3"), std::string::npos);
    EXPECT_NE(rendered.find("pmc_hotkey_hash_hits_total{hash=\"42\",shard=\"2\",node=\"node-a\"} 2"), std::string::npos);
}

TEST(MetricsCollectorTest, RenderShardInfoJsonIncludesPlacementMetadata) {
    metrics::ShardInfo shardInfo{"6", "6", "1", "3"};
    metrics::MetricsCollector collector("6", "node-b", shardInfo, false, 4);
    collector.setInFlightRequests(2);

    const std::string body = collector.renderShardInfoJson();

    EXPECT_NE(body.find("\"worker_index\":\"6\""), std::string::npos);
    EXPECT_NE(body.find("\"cpu\":\"6\""), std::string::npos);
    EXPECT_NE(body.find("\"numa_node\":\"1\""), std::string::npos);
    EXPECT_NE(body.find("\"nic_queue_id\":\"3\""), std::string::npos);
    EXPECT_NE(body.find("\"memory_arena_bytes\":"), std::string::npos);
    EXPECT_NE(body.find("\"in_flight_requests\":2"), std::string::npos);
}

} // namespace
