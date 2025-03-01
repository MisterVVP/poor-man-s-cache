
#pragma once
#include <future>
#include <optional>
#include <coroutine>
#include <thread>
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
                    std::suspend_never initial_suspend() { return {}; }
                    std::suspend_always final_suspend() noexcept { return {}; }

                    void unhandled_exception() {}
                    void return_void(){}

                    HandleReqTask get_return_object() { return HandleReqTask{handle_type::from_promise(*this)}; }
                    ~promise_type() {}
            };

            HandleReqTask(HandleReqTask &&rhs) : c_handle(rhs.c_handle) {
                rhs.c_handle = nullptr;
            }

            HandleReqTask &operator=(HandleReqTask &&rhs) {
                if (c_handle) {
                    c_handle.destroy();
                }
                c_handle = rhs.c_handle;
                rhs.c_handle = nullptr;
                return *this;
            }

            void runToCompletion() {
                if (!c_handle.done()) {
                    c_handle.resume();
                }
            }

            ~HandleReqTask() {
                if (c_handle) {
                    c_handle.destroy();
                }
            };

            friend class AcceptConnTask;
            friend class EventLoop;
    };

    class EventLoop : NonCopyable {
        public:
            class promise_type;
            using handle_type = std::coroutine_handle<promise_type>;
        private:
            handle_type c_handle;
            EventLoop(handle_type h) : c_handle(h) {};
        public:
            class promise_type {
                public:
                    int resultCode;

                    std::suspend_never initial_suspend() { return {}; }
                    std::suspend_always final_suspend() noexcept { return {}; }

                    void unhandled_exception() {}
                    void return_value(int rCode) {
                        resultCode = rCode;
                    }

                    EventLoop get_return_object() {
                        auto handle = handle_type::from_promise(*this);
                        return EventLoop{handle}; 
                    }
                    ~promise_type() {}
            };

            int finalResult() {
                return c_handle.promise().resultCode;
            }

            ~EventLoop() {
                if (c_handle) {
                    c_handle.destroy();
                }
            };
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
        friend class EventLoop;
    };


    class ThreadSwitchAwaiter {
        private:
            std::jthread* p_out;
        public:
            ThreadSwitchAwaiter(std::jthread* out):p_out(out){};
            bool await_ready() const noexcept { return false; }
            void await_suspend(std::coroutine_handle<>);
            void await_resume() const noexcept {}
    };
}