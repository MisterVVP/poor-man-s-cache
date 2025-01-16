#pragma once
#include <memory>
#include <prometheus/exposer.h>
#include <prometheus/registry.h>
#include <prometheus/counter.h>
#include <prometheus/histogram.h>
#include <prometheus/gauge.h>

using namespace prometheus;

// Prometheus metrics
auto registry = std::make_shared<Registry>();

auto& request_count = BuildCounter()
                          .Name("endpoint_tcp_requests_total")
                          .Help("Total number of TCP requests")
                          .Register(*registry)
                          .Add({});

auto& error_count = BuildCounter()
                        .Name("endpoint_tcp_errors_total")
                        .Help("Total number of TCP errors")
                        .Register(*registry)
                        .Add({});

auto& request_latency = BuildHistogram()
                            .Name("endpoint_tcp_request_latency_seconds")
                            .Help("Histogram of request latencies in seconds")
                            .Register(*registry)
                            .Add({}, Histogram::BucketBoundaries{0.01, 0.1, 0.5, 1.0, 2.5, 5.0});

auto& storage_num_entries = BuildGauge()
                         .Name("storage_num_entries")
                         .Help("Number of entries in the storage")
                         .Register(*registry)
                         .Add({});

auto& storage_num_resizes = BuildGauge()
                         .Name("storage_num_resizes")
                         .Help("Number of resizes in the storage")
                         .Register(*registry)
                         .Add({});


struct task {
    struct promise_type {
        task get_return_object() { return {}; }
        std::suspend_never initial_suspend() { return {}; }
        std::suspend_never final_suspend() noexcept { return {}; }
        void return_void() {}
        void unhandled_exception() {}
    };
};

auto switch_to_main() noexcept {
    struct awaitable {
        bool await_ready() const noexcept { return false; }
        void await_suspend(std::coroutine_handle<> handle) noexcept {}
        void await_resume() const noexcept {}
    };
    return awaitable{};
}
