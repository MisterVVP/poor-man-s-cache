#include "http_server.hpp"

#include <arpa/inet.h>
#include <chrono>
#include <cstring>
#include <netinet/in.h>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <sys/socket.h>
#include <unistd.h>

using namespace http;

namespace {
constexpr std::string_view METRICS_PATH = "/metrics";
constexpr std::string_view SHARD_PATH = "/shard";
constexpr std::string_view HEALTH_PATH = "/healthz";
constexpr std::string_view READY_PATH = "/readyz";
constexpr std::string_view HTTP_GET = "GET";

constexpr char HTTP_200_HEALTH[] =
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: text/plain\r\n"
    "Content-Length: 2\r\n"
    "Connection: close\r\n\r\n"
    "OK";

constexpr char HTTP_200_READY[] =
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: text/plain\r\n"
    "Content-Length: 6\r\n"
    "Connection: close\r\n\r\n"
    "READY\n";

constexpr char HTTP_503_NOT_READY[] =
    "HTTP/1.1 503 Service Unavailable\r\n"
    "Content-Type: text/plain\r\n"
    "Content-Length: 10\r\n"
    "Connection: close\r\n\r\n"
    "NOT_READY\n";

constexpr char HTTP_404_NOT_FOUND[] =
    "HTTP/1.1 404 Not Found\r\n"
    "Content-Type: text/plain\r\n"
    "Content-Length: 10\r\n"
    "Connection: close\r\n\r\n"
    "NOT_FOUND\n";

std::string_view parseGetPath(std::string_view request) {
    const auto lineEnd = request.find("\r\n");
    if (lineEnd == std::string_view::npos) {
        return {};
    }

    const auto line = request.substr(0, lineEnd);
    if (!line.starts_with(HTTP_GET)) {
        return {};
    }

    const auto firstSpace = line.find(' ');
    if (firstSpace == std::string_view::npos) {
        return {};
    }

    const auto secondSpace = line.find(' ', firstSpace + 1);
    if (secondSpace == std::string_view::npos || secondSpace <= firstSpace + 1) {
        return {};
    }

    return line.substr(firstSpace + 1, secondSpace - firstSpace - 1);
}
} // namespace

HttpServer::HttpServer(HttpServerConfig cfg, metrics::MetricsCollector& collector)
    : config(std::move(cfg)), collector(collector) {}

HttpServer::~HttpServer() { stop(); }

void HttpServer::start() {
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

void HttpServer::stop() {
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

void HttpServer::serveLoop() {
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

void HttpServer::handleClient(int client_fd) {
    constexpr std::size_t BUF_SIZE = 2048;
    char buffer[BUF_SIZE];
    ssize_t bytes_read = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
    if (bytes_read <= 0) {
        return;
    }

    buffer[bytes_read] = '\0';
    std::string_view request(buffer, static_cast<std::size_t>(bytes_read));
    const auto path = parseGetPath(request);

    std::string dynamicBody;
    std::string_view payload;
    std::string_view contentType = "text/plain; version=0.0.4";

    if (path == HEALTH_PATH) {
        payload = std::string_view(HTTP_200_HEALTH, sizeof(HTTP_200_HEALTH) - 1);
    } else if (path == READY_PATH) {
        const bool ready = config.readinessProbe ? config.readinessProbe() : true;
        payload = ready
            ? std::string_view(HTTP_200_READY, sizeof(HTTP_200_READY) - 1)
            : std::string_view(HTTP_503_NOT_READY, sizeof(HTTP_503_NOT_READY) - 1);
    } else if (path == METRICS_PATH) {
        dynamicBody = collector.renderPrometheus();
    } else if (path == SHARD_PATH) {
        dynamicBody = collector.renderShardInfoJson();
        contentType = "application/json";
    } else {
        payload = std::string_view(HTTP_404_NOT_FOUND, sizeof(HTTP_404_NOT_FOUND) - 1);
    }

    if (payload.empty()) {
        std::ostringstream response;
        response << "HTTP/1.1 200 OK\r\n";
        response << "Content-Type: " << contentType << "\r\n";
        response << "Content-Length: " << dynamicBody.size() << "\r\n";
        response << "Connection: close\r\n\r\n";
        response << dynamicBody;
        dynamicBody = response.str();
        payload = dynamicBody;
    }

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
