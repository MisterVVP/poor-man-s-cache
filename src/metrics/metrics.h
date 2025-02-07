#pragma once
#include <memory>
#include <format>
#include <prometheus/exposer.h>
#include <prometheus/registry.h>
#include <prometheus/counter.h>
#include <prometheus/histogram.h>
#include <prometheus/gauge.h>
#include "../server/server.h"

namespace metrics
{
    using namespace prometheus;
    using namespace server;

    class MetricsServer {
        private:
            std::string metrics_url;
            std::shared_ptr<Registry> registry;
            std::shared_ptr<Exposer> server;

            Gauge* server_num_active_connections = nullptr;
            Counter* server_num_requests_total = nullptr;
            Counter* server_num_errors_total = nullptr;

            void RegisterMetrics();

        public:
            MetricsServer(std::string metrics_url);
            void UpdateMetrics(CacheServerMetrics& serverMetrics);
    };
}