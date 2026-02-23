#pragma once

#include <atomic>
#include <functional>
#include <optional>
#include <string>
#include <thread>

#include "../metrics/metrics.hpp"

namespace http {

using ReadinessProbe = std::function<bool()>;

struct HttpServerConfig {
    std::string listenHost = "0.0.0.0";
    int listenPort = 9100;
    ReadinessProbe readinessProbe = {};
};

class HttpServer {
  public:
    HttpServer(HttpServerConfig config, metrics::MetricsCollector& collector);
    ~HttpServer();

    void start();
    void stop();

  private:
    void serveLoop();
    void handleClient(int client_fd);

    HttpServerConfig config;
    metrics::MetricsCollector& collector;
    std::optional<int> server_fd;
    std::thread worker;
    std::atomic<bool> running{false};
};

} // namespace http
