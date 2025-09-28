
#pragma once
#include <future>
#include <coroutine>
#include <optional>
#include <sys/socket.h>
#include "../non_copyable.hpp"

namespace server {
    /// @brief ReadRequest result codes
    enum ReqReadOperationResult : int_fast8_t {
        Failure = -2,
        Success = 0,
        AwaitingData = 1,
    };

    struct ReadRequestResult {
        ReqReadOperationResult operationResult;
    };

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

    class AsyncReadTask : NonCopyable {
        public:
            class promise_type;
            using handle_type = std::coroutine_handle<promise_type>;   
            int client_fd = -1; 
        private:
            handle_type c_handle;
            AsyncReadTask(handle_type h) : c_handle(h) {};
        public:
            class promise_type {
                public:
                    ReadRequestResult result;
                    std::suspend_never initial_suspend() { return {}; }
                    std::suspend_always final_suspend() noexcept { return {}; }

                    void unhandled_exception() {}
                    void return_value(ReadRequestResult res) {
                        result = res;
                    }

                    AsyncReadTask get_return_object() { return AsyncReadTask{handle_type::from_promise(*this)}; }
            };

            AsyncReadTask(AsyncReadTask &&art) : c_handle(art.c_handle), client_fd(art.client_fd) {
                art.c_handle = nullptr;
                art.client_fd = -1;
            }

            AsyncReadTask &operator=(AsyncReadTask &&art) {
                client_fd = art.client_fd;
                if (c_handle) {
                    c_handle.destroy();
                }
                c_handle = art.c_handle;
                art.c_handle = nullptr;
                return *this;
            }

            ~AsyncReadTask() {
                if (c_handle) {
                    c_handle.destroy();
                }
            }
            friend class AsyncReadAwaiter;
            friend class ReadRequestAwaiter;
            friend class HandleReqTask;
    };


    class ReadRequestAwaiter {
        private:
            std::coroutine_handle<AsyncReadTask::promise_type> c_handle;
        public:
            ReadRequestAwaiter(AsyncReadTask &art) : c_handle(art.c_handle) {}
            bool await_ready() const noexcept { return false; }

            std::coroutine_handle<> await_suspend(std::coroutine_handle<> h) {
                return h;
            }
            
            ReadRequestResult await_resume() {
                return c_handle.promise().result;
            }
    };

    class AsyncReadAwaiter {
        private:
            int fd;
            char* buffer;
            size_t bufSize;
        public:
            AsyncReadAwaiter(int fd, char* buffer, size_t bufSize) : fd(fd), buffer(buffer), bufSize(bufSize) {}
            bool await_ready() const noexcept { return false; }

            std::coroutine_handle<> await_suspend(std::coroutine_handle<> h) {
                return h;
            }

            ssize_t await_resume() {
                return ::read(fd, buffer, bufSize);
            }
    };

    class AsyncSendTask : NonCopyable {
        public:
            class promise_type;
            using handle_type = std::coroutine_handle<promise_type>;
        private:
            handle_type c_handle;
            AsyncSendTask(handle_type h) : c_handle(h) {};
        public:
            class promise_type {
                public:
                    std::suspend_never initial_suspend() { return {}; }
                    std::suspend_always final_suspend() noexcept { return {}; }

                    void unhandled_exception() {}
                    void return_void(){}

                    AsyncSendTask get_return_object() { return AsyncSendTask{handle_type::from_promise(*this)}; }
            };

            AsyncSendTask(AsyncSendTask &&art) : c_handle(art.c_handle){
                art.c_handle = nullptr;
            }

            AsyncSendTask &operator=(AsyncSendTask &&art) {
                if (c_handle) {
                    c_handle.destroy();
                }
                c_handle = art.c_handle;
                art.c_handle = nullptr;
                return *this;
            }

            ~AsyncSendTask() {
                if (c_handle) {
                    c_handle.destroy();
                }
            }
            friend class ProcessRequestAwaiter;
            friend class ProcessRequestTask;
    };

    class SuspendSelfAwaiter {
        public:
          bool await_ready() { return false; }
          std::coroutine_handle<> await_suspend(std::coroutine_handle<> h) {
              return h;
          }
          void await_resume() {}
      };
      

    class AsyncSendAwaiter {
        private:
            int fd;
            const msghdr* message;
        public:
            AsyncSendAwaiter(int fd, const msghdr* message) : fd(fd), message(message) {}
            bool await_ready() const noexcept { return false; }

            std::coroutine_handle<> await_suspend(std::coroutine_handle<> h) {
                return h;
            }

            ssize_t await_resume() {
                return ::sendmsg(fd, message, 0);
            }
    };

    class ProcessRequestTask : NonCopyable {
        public:
            class promise_type;
            using handle_type = std::coroutine_handle<promise_type>;    
        private:
            handle_type c_handle;
            ProcessRequestTask(handle_type h) : c_handle(h) {};
        public:
            class promise_type {
                public:
                    std::suspend_never initial_suspend() { return {}; }
                    std::suspend_always final_suspend() noexcept { return {}; }

                    void unhandled_exception() {}
                    void return_void() {}

                    ProcessRequestTask get_return_object() { return ProcessRequestTask{handle_type::from_promise(*this)}; }

                    SuspendSelfAwaiter await_transform(AsyncSendTask &ast) {
                        return SuspendSelfAwaiter{};
                    }
            };

            ProcessRequestTask(ProcessRequestTask &&hrt) : c_handle(hrt.c_handle) {
                hrt.c_handle = nullptr;
            }

            ProcessRequestTask &operator=(ProcessRequestTask &&hrt) {
                if (c_handle) {
                    c_handle.destroy();
                }
                c_handle = hrt.c_handle;
                hrt.c_handle = nullptr;
                return *this;
            }

            ~ProcessRequestTask() {
                if (c_handle) {
                    c_handle.destroy();
                }
            };
            friend class HandleReqTask;
            friend class ProcessRequestAwaiter;
    };

    class ProcessRequestAwaiter {
        private:
            std::coroutine_handle<ProcessRequestTask::promise_type> c_handle;
        public:
            ProcessRequestAwaiter(ProcessRequestTask &prt) : c_handle(prt.c_handle) {}
            bool await_ready() const noexcept { return false; }

            std::coroutine_handle<> await_suspend(std::coroutine_handle<> h) {
                return h;
            }
            
            void await_resume() {}
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
                    int event_count;

                    void return_value(int value) {
                        event_count = value;
                    }

                    std::suspend_always yield_value(int value) {
                        event_count = value;
                        return {};
                    }

                    HandleReqTask get_return_object() { return HandleReqTask{handle_type::from_promise(*this)}; }

                    ReadRequestAwaiter await_transform(AsyncReadTask &art) {
                        return ReadRequestAwaiter{art};
                    }

                    ProcessRequestAwaiter await_transform (ProcessRequestTask &prt) {
                        return ProcessRequestAwaiter{prt};
                    }
            };

            int next_value() {
                auto &promise = c_handle.promise();
                if (!c_handle.done()) {
                    c_handle.resume();
                }
                return promise.event_count;
            }

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
            friend class SuspendSelfAwaiter;
    };
}
