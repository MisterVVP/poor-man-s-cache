#include <gtest/gtest.h>

#include <string>
#include <atomic>
#include <thread>
#include <chrono>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>

#include "metrics.hpp"
#include "../http/http_server.hpp"

namespace {


std::string sendHttpRequest(int port, std::string_view request) {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    EXPECT_GE(fd, 0);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    EXPECT_EQ(::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)), 0);

    auto sent = ::send(fd, request.data(), request.size(), 0);
    EXPECT_EQ(sent, static_cast<ssize_t>(request.size()));

    std::string response;
    char buffer[1024];
    while (true) {
        auto n = ::recv(fd, buffer, sizeof(buffer), 0);
        if (n <= 0) {
            break;
        }
        response.append(buffer, static_cast<std::size_t>(n));
    }

    ::close(fd);
    return response;
}

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
    collector.setWorkerState(metrics::WorkerState::Ready);
    collector.incrementShutdown(metrics::ShutdownReason::Sigterm);
    collector.incrementShutdown(metrics::ShutdownReason::Sigint);
    collector.incrementShutdown(metrics::ShutdownReason::Other);
    collector.incrementDrainTimeout();
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
    EXPECT_NE(rendered.find("pmc_worker_state{shard=\"2\",node=\"node-a\"} 1"), std::string::npos);
    EXPECT_NE(rendered.find("pmc_shutdowns_total{reason=\"sigterm\",shard=\"2\",node=\"node-a\"} 1"), std::string::npos);
    EXPECT_NE(rendered.find("pmc_shutdowns_total{reason=\"sigint\",shard=\"2\",node=\"node-a\"} 1"), std::string::npos);
    EXPECT_NE(rendered.find("pmc_shutdowns_total{reason=\"other\",shard=\"2\",node=\"node-a\"} 1"), std::string::npos);
    EXPECT_NE(rendered.find("pmc_drain_timeout_total{shard=\"2\",node=\"node-a\"} 1"), std::string::npos);
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


TEST(MetricsHttpServerTest, HealthzAndReadyzReflectProbeState) {
    std::atomic<bool> ready{false};
    metrics::ShardInfo shardInfo{"1", "0", "0", "0"};
    metrics::MetricsCollector collector("1", "node", shardInfo, false, 4);

    http::HttpServerConfig config{"127.0.0.1", 19100};
    config.readinessProbe = [&ready]() noexcept { return ready.load(std::memory_order_acquire); };

    http::HttpServer server(config, collector);
    server.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    const auto health = sendHttpRequest(19100, "GET /healthz HTTP/1.1\r\nHost: localhost\r\n\r\n");
    EXPECT_NE(health.find("HTTP/1.1 200 OK"), std::string::npos);

    const auto notReady = sendHttpRequest(19100, "GET /readyz HTTP/1.1\r\nHost: localhost\r\n\r\n");
    EXPECT_NE(notReady.find("HTTP/1.1 503 Service Unavailable"), std::string::npos);

    ready.store(true, std::memory_order_release);
    const auto readyResponse = sendHttpRequest(19100, "GET /readyz HTTP/1.1\r\nHost: localhost\r\n\r\n");
    EXPECT_NE(readyResponse.find("HTTP/1.1 200 OK"), std::string::npos);

    server.stop();
}

} // namespace
