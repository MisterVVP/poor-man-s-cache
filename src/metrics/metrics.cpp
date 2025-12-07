#include "metrics.hpp"

#include <arpa/inet.h>
#include <chrono>
#include <cstring>
#include <netinet/in.h>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <sys/socket.h>
#include <unistd.h>

using namespace metrics;

namespace {

constexpr std::string_view METRICS_PATH = "/metrics";
constexpr std::string_view HTTP_GET = "GET";

std::string buildLabels(std::string_view shard, std::string_view node, std::string_view extra = {}) {
    std::ostringstream oss;
    oss << '{';
    if (!extra.empty()) {
        oss << extra << ',';
    }
    oss << "shard=\"" << shard << "\",node=\"" << node << "\"}";
    return oss.str();
}

void appendMetric(std::ostringstream& oss, std::string_view name, std::string_view labels, uint64_t value) {
    oss << name << labels << ' ' << value << '\n';
}

} // namespace

MetricsCollector::MetricsCollector(std::string shardLabel, std::string nodeLabel)
    : shard(std::move(shardLabel)), node(std::move(nodeLabel)) {}

void MetricsCollector::incrementRequest(RequestOperation op) noexcept {
    std::scoped_lock lock(mutex);
    switch (op) {
        case RequestOperation::Get:
            ++metrics.requestsGet;
            break;
        case RequestOperation::Set:
            ++metrics.requestsSet;
            break;
        case RequestOperation::Del:
            ++metrics.requestsDel;
            break;
        case RequestOperation::Other:
        default:
            ++metrics.requestsOther;
            break;
    }
}

void MetricsCollector::incrementResponse(ResponseStatus status) noexcept {
    std::scoped_lock lock(mutex);
    switch (status) {
        case ResponseStatus::Ok:
            ++metrics.responsesOk;
            break;
        case ResponseStatus::NotFound:
            ++metrics.responsesNotFound;
            break;
        case ResponseStatus::Error:
        default:
            ++metrics.responsesError;
            break;
    }
}

void MetricsCollector::addBytesRx(std::size_t amount) noexcept {
    std::scoped_lock lock(mutex);
    metrics.bytesRx += amount;
}

void MetricsCollector::addBytesTx(std::size_t amount) noexcept {
    std::scoped_lock lock(mutex);
    metrics.bytesTx += amount;
}

void MetricsCollector::connectionAccepted() noexcept {
    std::scoped_lock lock(mutex);
    ++metrics.connectionsAccepted;
    ++metrics.connectionsCurrent;
}

void MetricsCollector::connectionClosed(CloseReason reason) noexcept {
    std::scoped_lock lock(mutex);
    if (metrics.connectionsCurrent > 0) {
        --metrics.connectionsCurrent;
    }

    switch (reason) {
        case CloseReason::Client:
            ++metrics.connectionsClosedClient;
            break;
        case CloseReason::Error:
            ++metrics.connectionsClosedError;
            break;
        case CloseReason::Server:
        default:
            ++metrics.connectionsClosedServer;
            break;
    }
}

void MetricsCollector::recordBatch(std::size_t requestsInBatch) noexcept {
    std::scoped_lock lock(mutex);
    ++metrics.batchesTotal;
    metrics.requestsPerBatchSum += requestsInBatch;
    ++metrics.requestsPerBatchCount;
}

void MetricsCollector::setWriteQueueDepth(std::size_t depth) noexcept {
    std::scoped_lock lock(mutex);
    metrics.writeQueueDepth = depth;
}

void MetricsCollector::setReadBufferUsageBytes(std::size_t bytes) noexcept {
    std::scoped_lock lock(mutex);
    metrics.readBufferUsageBytes = bytes;
}

void MetricsCollector::setKvsState(std::size_t items, std::size_t bytesUsed) noexcept {
    std::scoped_lock lock(mutex);
    metrics.kvsItems = items;
    metrics.kvsBytesUsed = bytesUsed;
}

MetricsSnapshot MetricsCollector::snapshot() const noexcept {
    std::scoped_lock lock(mutex);
    return metrics;
}

std::string MetricsCollector::renderPrometheus() const {
    auto snap = snapshot();
    const auto labels = buildLabels(shard, node);
    std::ostringstream oss;

    appendMetric(oss, "pmc_requests_total", buildLabels(shard, node, "op=\"get\""), snap.requestsGet);
    appendMetric(oss, "pmc_requests_total", buildLabels(shard, node, "op=\"set\""), snap.requestsSet);
    appendMetric(oss, "pmc_requests_total", buildLabels(shard, node, "op=\"del\""), snap.requestsDel);
    appendMetric(oss, "pmc_requests_total", buildLabels(shard, node, "op=\"other\""), snap.requestsOther);

    appendMetric(oss, "pmc_responses_total", buildLabels(shard, node, "status=\"ok\""), snap.responsesOk);
    appendMetric(oss, "pmc_responses_total", buildLabels(shard, node, "status=\"not_found\""), snap.responsesNotFound);
    appendMetric(oss, "pmc_responses_total", buildLabels(shard, node, "status=\"error\""), snap.responsesError);

    appendMetric(oss, "pmc_bytes_rx_total", labels, snap.bytesRx);
    appendMetric(oss, "pmc_bytes_tx_total", labels, snap.bytesTx);

    appendMetric(oss, "pmc_connections_current", labels, snap.connectionsCurrent);
    appendMetric(oss, "pmc_connections_accepted_total", labels, snap.connectionsAccepted);
    appendMetric(oss, "pmc_connections_closed_total", buildLabels(shard, node, "reason=\"client\""), snap.connectionsClosedClient);
    appendMetric(oss, "pmc_connections_closed_total", buildLabels(shard, node, "reason=\"server\""), snap.connectionsClosedServer);
    appendMetric(oss, "pmc_connections_closed_total", buildLabels(shard, node, "reason=\"error\""), snap.connectionsClosedError);

    appendMetric(oss, "pmc_batches_total", labels, snap.batchesTotal);
    appendMetric(oss, "pmc_requests_per_batch_sum", labels, snap.requestsPerBatchSum);
    appendMetric(oss, "pmc_requests_per_batch_count", labels, snap.requestsPerBatchCount);

    appendMetric(oss, "pmc_kvs_items", labels, snap.kvsItems);
    appendMetric(oss, "pmc_kvs_bytes_used", labels, snap.kvsBytesUsed);
    appendMetric(oss, "pmc_kvs_evictions_total", labels, snap.kvsEvictions);

    appendMetric(oss, "pmc_write_queue_depth", labels, snap.writeQueueDepth);
    appendMetric(oss, "pmc_read_buffer_usage_bytes", labels, snap.readBufferUsageBytes);

    return oss.str();
}

MetricsHttpServer::MetricsHttpServer(MetricsConfig cfg, MetricsCollector& collector)
    : config(std::move(cfg)), collector(collector) {}

MetricsHttpServer::~MetricsHttpServer() { stop(); }

void MetricsHttpServer::start() {
    if (running.load()) {
        return;
    }

    server_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (!server_fd || *server_fd < 0) {
        throw std::runtime_error("Failed to create metrics socket");
    }

    int flag = 1;
    if (setsockopt(*server_fd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag)) == -1) {
        throw std::runtime_error("Failed to set SO_REUSEADDR for metrics socket");
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<uint16_t>(config.listenPort));
    address.sin_addr.s_addr = inet_addr(config.listenHost.c_str());

    if (bind(*server_fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == -1) {
        throw std::runtime_error("Failed to bind metrics socket");
    }

    if (listen(*server_fd, 16) == -1) {
        throw std::runtime_error("Failed to listen on metrics socket");
    }

    running = true;
    worker = std::thread([this]() { serveLoop(); });
}

void MetricsHttpServer::stop() {
    if (!running.exchange(false)) {
        return;
    }

    if (server_fd && *server_fd >= 0) {
        ::shutdown(*server_fd, SHUT_RDWR);
        ::close(*server_fd);
    }

    if (worker.joinable()) {
        worker.join();
    }
}

void MetricsHttpServer::serveLoop() {
    while (running.load()) {
        sockaddr_in client{};
        socklen_t client_len = sizeof(client);
        int client_fd = accept(server_fd.value(), reinterpret_cast<sockaddr*>(&client), &client_len);
        if (client_fd < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (!running.load()) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        handleClient(client_fd);
        ::close(client_fd);
    }
}

void MetricsHttpServer::handleClient(int client_fd) {
    constexpr std::size_t BUF_SIZE = 1024;
    char buffer[BUF_SIZE];
    ssize_t bytes_read = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
    if (bytes_read <= 0) {
        return;
    }

    buffer[bytes_read] = '\0';
    std::string_view request(buffer, static_cast<std::size_t>(bytes_read));

    auto endOfLine = request.find('\n');
    if (endOfLine == std::string_view::npos) {
        return;
    }

    auto firstLine = request.substr(0, endOfLine);
    if (!firstLine.starts_with(HTTP_GET) || firstLine.find(METRICS_PATH) == std::string_view::npos) {
        return;
    }

    const auto body = collector.renderPrometheus();
    std::ostringstream response;
    response << "HTTP/1.1 200 OK\r\n";
    response << "Content-Type: text/plain; version=0.0.4\r\n";
    response << "Content-Length: " << body.size() << "\r\n";
    response << "Connection: close\r\n\r\n";
    response << body;

    auto payload = response.str();
    auto remaining = payload.size();
    const char* data = payload.data();

    while (remaining > 0) {
        auto sent = send(client_fd, data, remaining, MSG_NOSIGNAL);
        if (sent <= 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        remaining -= static_cast<std::size_t>(sent);
        data += sent;
    }
}
