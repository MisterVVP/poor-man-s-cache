
#pragma once
#include <future>
#include <optional>
#include <coroutine>
#include "../non_copyable.h"

namespace server {

    /// @brief Server status results
    enum ServerStatus : int_fast8_t {
        Terminated = -2,     ///< Stopped due to a fatal failure or unrecoverable error
        Error = -1,          ///< Generic error state
        Stopped = 0,         ///< Gracefully stopped
        NotReady = 1,        ///< Initialization phase, not yet running
        Running = 2,         ///< Fully operational
        Processing = 3       ///< Actively handling client requests
    };

    struct EpollStatus
    {
        public:
            int epoll_fd;
            ServerStatus status;

            static EpollStatus NotReady() { return EpollStatus{ServerStatus::NotReady };}
            static EpollStatus Terminated() { return EpollStatus{ServerStatus::Terminated };}
            static EpollStatus Stopped() { return EpollStatus{ServerStatus::Stopped };}
            static EpollStatus Running(int fd) { return EpollStatus{fd, ServerStatus::Running };}
            static EpollStatus Processing(int fd) { return EpollStatus{fd, ServerStatus::Processing };}
        
        private:
            EpollStatus(int fd, ServerStatus status): epoll_fd(fd), status(status) {}
            EpollStatus(ServerStatus status): epoll_fd(-1), status(status) {}
            EpollStatus();
    };

    class IncomingConnectionAwaiter;
    class HandleEventAwaiter;
    class AcceptConnTask;

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

                    std::suspend_never initial_suspend() { return {}; }
                    std::suspend_always final_suspend() noexcept { return {}; }
                    IncomingConnectionAwaiter await_transform(const AcceptConnTask &acCoro);

                    void unhandled_exception() {}
                    void return_value(uint_fast8_t nFailedEvents){
                        numFailedEvents = nFailedEvents;
                    }

                    HandleReqTask get_return_object() { return HandleReqTask{handle_type::from_promise(*this)}; }
                    promise_type(): numFailedEvents(0) {}
                    ~promise_type() {}
            };

            uint_fast8_t getProcessingResult() {
                return c_handle.promise().numFailedEvents;
            }

            ~HandleReqTask() {
                if (c_handle) {
                    c_handle.destroy();
                }
            };

            friend class AcceptConnTask;
    };

    class AcceptConnTask : NonCopyable {
        public:
            class promise_type;
            using handle_type = std::coroutine_handle<promise_type>;
        private:
            handle_type c_handle;
            AcceptConnTask(handle_type h) : c_handle(h) {}
        public:
            class promise_type {
                public:
                    std::optional<EpollStatus> eStatus;

                    std::suspend_always initial_suspend() { return {}; }
                    std::suspend_always final_suspend() noexcept { return {}; }
                    HandleEventAwaiter await_transform(HandleReqTask &hr);

                    void unhandled_exception() {}

                    std::suspend_always yield_value(EpollStatus eStat) {
                        eStatus = eStat;
                        return {};
                    }

                    void return_value(EpollStatus eStat) {
                        eStatus = eStat;
                    }

                    AcceptConnTask get_return_object() {
                        auto handle = handle_type::from_promise(*this);
                        return AcceptConnTask{handle}; 
                    }

                    promise_type(): eStatus(EpollStatus::NotReady()){}
                    ~promise_type() {}
            };

            std::optional<EpollStatus> getStatus() {
                auto &promise = c_handle.promise();
                promise.eStatus = std::nullopt;
                if (!c_handle.done()) {
                    c_handle.resume();
                }
                return c_handle.promise().eStatus;
            }

            ~AcceptConnTask() {
                if (c_handle) {
                    c_handle.destroy();
                }
            };
        friend class HandleReqTask;
    };


    class IncomingConnectionAwaiter {
        private:
            AcceptConnTask::promise_type *promise;
        public:
            IncomingConnectionAwaiter(AcceptConnTask::promise_type *promise);

            bool await_ready() { return false; }
            std::coroutine_handle<> await_suspend(std::coroutine_handle<>);
            void await_resume() const noexcept {}
    };

    class HandleEventAwaiter {
        private:
            HandleReqTask::promise_type *promise;
        public:
            HandleEventAwaiter(HandleReqTask::promise_type *promise);
            bool await_ready() const noexcept { return false; }
            std::coroutine_handle<> await_suspend(std::coroutine_handle<>);
            void await_resume() const noexcept {}
    };
}