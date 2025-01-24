#pragma once
#include <memory>
#include <coroutine>
#include <functional>
#include <format>
#include <prometheus/exposer.h>
#include <prometheus/registry.h>
#include <prometheus/counter.h>
#include <prometheus/histogram.h>
#include <prometheus/gauge.h>
#include "../kvs.h"

namespace metrics
{
    using namespace prometheus;

    class MetricsServer {
        private:
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
            };

            std::string metrics_url;
            std::shared_ptr<Registry> registry;
            std::shared_ptr<Exposer> server;

            std::shared_ptr<KeyValueStore> keyValueStore_ptr;
            Gauge* storage_num_entries = nullptr;
            Gauge* storage_num_resizes = nullptr;
            Histogram* request_latency = nullptr;
            Counter* error_count = nullptr;
            Counter* request_count = nullptr;            

            void RegisterMetrics();

        public:
            MetricsServer(std::string metrics_url, std::shared_ptr<KeyValueStore> kvs_ptr);
            ~MetricsServer();
            void IncNumErrors();
            task UpdateMetrics();
    };
}