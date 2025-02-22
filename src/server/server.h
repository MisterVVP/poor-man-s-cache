#pragma once
#include <atomic>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <functional>
#include <thread>
#include <vector>
#include <queue>
#include <future>
#include <coroutine>
#include <semaphore>
#include <fcntl.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include "../hash/hash.h"
#include "../non_copyable.h"
#include "sockutils.h"
#include "conn_manager.hpp"
#include "shard.h"
#include "constants.hpp"

namespace server {

    using namespace kvs;

    struct CacheServerMetrics {
        uint_fast64_t serverNumErrors = 0;
        uint_fast32_t serverNumActiveConnections = 0;
        uint_fast64_t serverNumRequests = 0;

        CacheServerMetrics(uint_fast64_t numErrors, uint_fast32_t numConnections, uint_fast64_t numRequests):
            serverNumErrors(numErrors), serverNumActiveConnections(numConnections), serverNumRequests(numRequests) {
        }
    };

    struct ServerSettings {
        /// @brief Server port
        int port = 9001;
        /// @brief Number of server shards, increase for stability and performance, decrease to save server resources
        uint_fast16_t numShards = 24;
        /// @brief Requested buffer size for server socket
        int sockBuffer = 1048576;
        /// @brief Enable compression of stored values. Disable if RPS and processing speed is more important than memory consumption
        bool enableCompression = false;
    };

    /// @brief Orchestrator results
    enum OrchestratorResult : int_fast8_t {
        Interrupted = -3,
        AcceptConnError = -2,
        EpollCtlAddError = -1,
        Success = 0,
    };

    /// @brief Da biggest and da mainest coroutine in this program
    class BigBossCoro : NonCopyable {
        public:
            class promise_type;
            using handle_type = std::coroutine_handle<promise_type>;
        private:
            handle_type c_handle;
            BigBossCoro(handle_type h) : c_handle(h) {};
        public:
            class promise_type {
                public:
                    OrchestratorResult resultCode = OrchestratorResult::Success;
                    
                    std::suspend_never initial_suspend() { return {}; }
                    std::suspend_always final_suspend() noexcept { return {}; }
                    std::suspend_always yield_value(OrchestratorResult rCode) {
                        resultCode = rCode;
                        return {};
                    }

                    void unhandled_exception() {}
                    void return_value(OrchestratorResult rCode) {
                        resultCode = rCode;
                    }

                    BigBossCoro get_return_object() {
                        auto handle = handle_type::from_promise(*this);
                        return BigBossCoro{handle}; 
                    }
                    promise_type(): resultCode(OrchestratorResult::Interrupted){}
                    ~promise_type() {}
            };


            OrchestratorResult next_value() {
                auto &promise = c_handle.promise();
                promise.resultCode = OrchestratorResult::Interrupted;
                c_handle.resume();
                return promise.resultCode;
            };

            OrchestratorResult final_result() {
                return c_handle.promise().resultCode;
            }

            ~BigBossCoro() {
                if (c_handle) {
                    c_handle.destroy();
                }
            };
    };

    class HandleReqTask : NonCopyable {
        public:
            class promise_type;
            using handle_type = std::coroutine_handle<promise_type>;
        private:
            handle_type c_handle;
            HandleReqTask(handle_type h) : c_handle(h) {};
        public:
            class promise_type {
                public:
                    uint_fast8_t numFailedEvents;
                    HandleReqTask get_return_object() { return HandleReqTask{handle_type::from_promise(*this)}; }
                    std::suspend_always initial_suspend() { return {}; }
                    std::suspend_always final_suspend() noexcept { return {}; }
                    void unhandled_exception() {}
                    void return_value(uint_fast8_t nFailedEvents) {
                        numFailedEvents = nFailedEvents;
                    }
                    promise_type(): numFailedEvents(0) {}
                    ~promise_type() {}
            };

            uint_fast8_t final_result() {
                return c_handle.promise().numFailedEvents;
            }

            ~HandleReqTask() {
                if (c_handle) {
                    c_handle.destroy();
                }
            };
            friend class HandreReqAwaiter;    
    };

    class HandreReqAwaiter {
        private:
            std::coroutine_handle<> handle;
        public:
            HandreReqAwaiter(HandleReqTask &hr) : handle(hr.c_handle) {}
            bool await_ready() const noexcept { return false; }
            std::coroutine_handle<> await_suspend(std::coroutine_handle<>) {
                return handle;
            }
            void await_resume() const noexcept {}
    };

    class NoopAwaiter {
        public:
            bool await_ready() const noexcept { return false; }
            void await_suspend(std::coroutine_handle<> handle) noexcept {}
            void await_resume() const noexcept {}
    };

    class CacheServer : NonCopyableOrMovable {
        private:
            struct RequestPart {
                size_t size;
                char* part;
                size_t location;

                RequestPart(char* part, size_t size, size_t location): part(part), size(size), location(location){}
            };

            /// @brief ReadRequest result codes
            enum ReqReadOperationResult : int_fast8_t{
                Failure = -1,
                Success = 0,
            };

            struct ReadRequestResult {
                private:
                    ReadRequestResult(ReqReadOperationResult res) : operationResult(res){};

                public:
                    std::string request;
                    ReqReadOperationResult operationResult;

                    ReadRequestResult(std::string request, ReqReadOperationResult res) : request(request), operationResult(res){};

                    static ReadRequestResult Success(std::string request) noexcept {
                        return ReadRequestResult { request, ReqReadOperationResult::Success };
                    };

                    static ReadRequestResult Failure() noexcept {
                        return ReadRequestResult { ReqReadOperationResult::Failure };
                    };
            };

            std::binary_semaphore metricsSemaphore{0};
            std::atomic<uint_fast64_t> numErrors = 0;
            std::atomic<uint_fast64_t> numRequests = 0;
            std::atomic<bool>& cancellationToken;
            std::atomic<bool> isRunning = false;
            std::jthread metricsUpdaterThread;
            ConnManager connManager;
            
            uint_fast16_t numShards;
            std::vector<ServerShard> serverShards;
            int port;
            int server_fd;
            int epoll_fd;
            epoll_event epoll_events[MAX_EVENTS];

            ReadRequestResult readRequest(int client_fd);
            HandleReqTask handleRequests();
            void sendResponse(int client_fd, const char* response, const size_t responseSize);
            void metricsUpdater(std::queue<CacheServerMetrics>& channel, std::stop_token stopToken);
            BigBossCoro handleBigProblems(AcceptConnTask& acCoro);
        public:
            CacheServer(std::atomic<bool>& cToken, const ServerSettings settings = ServerSettings{});
            ~CacheServer();

            /// @brief Starts processing incoming requests
            /// @param channel metrics queue to report to
            /// @return operation result, 0 - success, other values - failure
            int Start(std::queue<CacheServerMetrics>& channel);

            /// @brief Gracefully stops server, restart is not (yet) supported
            void Stop() noexcept;
    };
}
