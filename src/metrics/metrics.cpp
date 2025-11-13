#include "metrics.hpp"
#include "../server/server.hpp"
#include "../server/constants.hpp"

using namespace metrics;

static Histogram::BucketBoundaries CreateLinearBuckets(std::int64_t start, std::int64_t end, std::int64_t step) {
    auto bucket_boundaries = Histogram::BucketBoundaries{};
    for (auto i = start; i < end; i += step) {
        bucket_boundaries.push_back(i);
    }
    return bucket_boundaries;
}

void MetricsServer::RegisterMetrics()
{
    server_num_errors_total = &BuildCounter()
                        .Name("server_num_errors_total")
                        .Help("Total number of TCP server errors")
                        .Register(*registry)
                        .Add({});

    server_num_active_connections = &BuildGauge()
                        .Name("server_num_active_connections")
                        .Help("Number of active connections")
                        .Register(*registry)
                        .Add({});

    server_num_requests_total = &BuildCounter()
                        .Name("server_num_requests_total")
                        .Help("Total number of server requests")
                        .Register(*registry)
                        .Add({});
}

MetricsServer::MetricsServer(std::string metrics_url)
{
    registry = std::make_shared<Registry>();
    server = std::make_shared<Exposer>(metrics_url);
    RegisterMetrics();
    server.get()->RegisterCollectable(registry);
    std::cout << "Metrics server started on " << metrics_url << std::endl;
}

void MetricsServer::UpdateMetrics(CacheServerMetrics& serverMetrics)
{
    server_num_active_connections->Set(serverMetrics.numActiveConnections);

    auto numErrorsInc = serverMetrics.numErrors - server_num_errors_total->Value();
    server_num_errors_total->Increment(numErrorsInc);

    auto numRequestsInc = serverMetrics.numRequests - server_num_requests_total->Value();
    server_num_requests_total->Increment(numRequestsInc);
}
