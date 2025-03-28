
#pragma once
#include <future>
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

            HandleReqTask(HandleReqTask &&hrt) : c_handle(hrt.c_handle) {
                hrt.c_handle = nullptr;
            }

            HandleReqTask &operator=(HandleReqTask &&hrt) {
                if (c_handle) {
                    c_handle.destroy();
                }
                c_handle = hrt.c_handle;
                hrt.c_handle = nullptr;
                return *this;
            }

            ~HandleReqTask() {
                if (c_handle) {
                    c_handle.destroy();
                }
            };
            friend class EventLoop;
            friend class HandleReqAwaiter;
    };
    
    struct HandleReqAwaiter {
        private:
            HandleReqTask::promise_type *promise;
        public:
            HandleReqAwaiter(HandleReqTask::promise_type *p) : promise(p) {}
            bool await_ready() const noexcept { return false; }
            std::coroutine_handle<> await_suspend(std::coroutine_handle<> h);
            void await_resume() noexcept {}
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
                EpollStatus eStatus;

                std::suspend_never initial_suspend() { return {}; }
                std::suspend_always final_suspend() noexcept {                    
                    return {}; 
                }
        
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
        
                promise_type() : eStatus(EpollStatus::NotReady()) {}
                ~promise_type() {}
            };

            AcceptConnTask(AcceptConnTask &&act) : c_handle(act.c_handle) {
                act.c_handle = nullptr;
            }

            AcceptConnTask &operator=(AcceptConnTask &&act) {
                if (c_handle) {
                    c_handle.destroy();
                }
                c_handle = act.c_handle;
                act.c_handle = nullptr;
                return *this;
            }

            ~AcceptConnTask() {
                if (c_handle) {
                    c_handle.destroy();
                }
            };
        friend class EventLoop;
    };

    class ConnAwaiter {
        private:
            AcceptConnTask::promise_type *promise;
        public:
            ConnAwaiter(AcceptConnTask::promise_type *p) : promise(p) {}
            bool await_ready() const noexcept { return false; }
            std::coroutine_handle<> await_suspend(std::coroutine_handle<> h);
            EpollStatus await_resume();
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

                    HandleReqAwaiter await_transform(HandleReqTask &hrt) {
                        auto &promise = hrt.c_handle.promise();
                        return HandleReqAwaiter{&promise};
                    }

                    ConnAwaiter await_transform(AcceptConnTask &ac) {
                        auto &promise = ac.c_handle.promise();
                        return ConnAwaiter{&promise};
                    }
            };

            int finalResult() {
                return c_handle.promise().resultCode;
            }

            EventLoop(EventLoop &&el) : c_handle(el.c_handle) {
                el.c_handle = nullptr;
            }

            EventLoop &operator=(EventLoop &&el) {
                if (c_handle) {
                    c_handle.destroy();
                }
                c_handle = el.c_handle;
                el.c_handle = nullptr;
                return *this;
            }

            ~EventLoop() {
                if (c_handle) {
                    c_handle.destroy();
                }
            };
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