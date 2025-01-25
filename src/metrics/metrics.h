#pragma once
#include <memory>
#include <format>
#include <prometheus/exposer.h>
#include <prometheus/registry.h>
#include <prometheus/counter.h>
#include <prometheus/histogram.h>
#include <prometheus/gauge.h>
#include "../server.h"

namespace metrics
{
    using namespace prometheus;

    class MetricsServer {
        private:
            std::string metrics_url;
            std::shared_ptr<Registry> registry;
            std::shared_ptr<Exposer> server;

            Gauge* storage_num_entries = nullptr;
            Gauge* storage_num_resizes = nullptr;
            Histogram* request_latency = nullptr;
            Gauge* server_num_errors = nullptr;
            Counter* request_count = nullptr;            

            void RegisterMetrics();

        public:
            MetricsServer(std::string metrics_url);
            void UpdateMetrics(CacheServerMetrics& serverMetrics);
    };
}