#include "metrics.h"

using namespace metrics;

void MetricsServer::RegisterMetrics()
{
    storage_num_entries = &BuildGauge()
                            .Name("storage_num_entries")
                            .Help("Number of entries in the storage")
                            .Register(*registry)
                            .Add({});

    storage_num_resizes = &BuildGauge()
                            .Name("storage_num_resizes")
                            .Help("Number of resizes in the storage")
                            .Register(*registry)
                            .Add({});
    request_count = &BuildCounter()
                          .Name("endpoint_tcp_requests_total")
                          .Help("Total number of TCP requests")
                          .Register(*registry)
                          .Add({});

    error_count = &BuildCounter()
                        .Name("endpoint_tcp_errors_total")
                        .Help("Total number of TCP errors")
                        .Register(*registry)
                        .Add({});

    request_latency = &BuildHistogram()
                            .Name("endpoint_tcp_request_latency_seconds")
                            .Help("Histogram of request latencies in seconds")
                            .Register(*registry)
                            .Add({}, Histogram::BucketBoundaries{0.01, 0.1, 0.5, 1.0, 2.5, 5.0});
}

MetricsServer::MetricsServer(std::string metrics_url, std::shared_ptr<KeyValueStore> kvs_ptr)
{
    registry = std::make_shared<Registry>();
    server = std::make_shared<Exposer>(metrics_url);
    keyValueStore_ptr = kvs_ptr;
    RegisterMetrics();
    server.get()->RegisterCollectable(registry);
    std::cout << "Metrics server started on " << metrics_url << std::endl;
}

MetricsServer::~MetricsServer() {

}

MetricsServer::task MetricsServer::UpdateMetrics()
{
    for (;;) {
        storage_num_entries->Set(keyValueStore_ptr.get()->getNumEntries());
        storage_num_resizes->Set(keyValueStore_ptr.get()->getNumResizes());
        co_await switch_to_main();
    }
}

void MetricsServer::IncNumErrors()
{
    error_count->Increment();
}
