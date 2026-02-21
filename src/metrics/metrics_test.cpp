#include <gtest/gtest.h>

#include <string>

#include "metrics.hpp"

namespace {

TEST(MetricsCollectorTest, RenderPrometheusIncludesKvsAndTrafficMetrics) {
    metrics::MetricsCollector collector("2", "node-a");

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
    collector.setWriteQueueDepth(11);
    collector.setReadBufferUsageBytes(22);
    collector.setKvsState(4, 321);

    const std::string rendered = collector.renderPrometheus();

    EXPECT_NE(rendered.find("pmc_requests_total{op=\"get\",shard=\"2\",node=\"node-a\"} 1"), std::string::npos);
    EXPECT_NE(rendered.find("pmc_responses_total{status=\"ok\",shard=\"2\",node=\"node-a\"} 1"), std::string::npos);
    EXPECT_NE(rendered.find("pmc_bytes_rx_total{shard=\"2\",node=\"node-a\"} 128"), std::string::npos);
    EXPECT_NE(rendered.find("pmc_bytes_tx_total{shard=\"2\",node=\"node-a\"} 64"), std::string::npos);
    EXPECT_NE(rendered.find("pmc_connections_current{shard=\"2\",node=\"node-a\"} 1"), std::string::npos);
    EXPECT_NE(rendered.find("pmc_batches_total{shard=\"2\",node=\"node-a\"} 1"), std::string::npos);
    EXPECT_NE(rendered.find("pmc_write_queue_depth{shard=\"2\",node=\"node-a\"} 11"), std::string::npos);
    EXPECT_NE(rendered.find("pmc_read_buffer_usage_bytes{shard=\"2\",node=\"node-a\"} 22"), std::string::npos);
    EXPECT_NE(rendered.find("pmc_kvs_items{shard=\"2\",node=\"node-a\"} 4"), std::string::npos);
    EXPECT_NE(rendered.find("pmc_kvs_bytes_used{shard=\"2\",node=\"node-a\"} 321"), std::string::npos);
}

} // namespace
