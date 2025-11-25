#pragma once
#include <cstdint>
#include <atomic>
#include <coroutine>
#include <unordered_map>
#include <vector>
#include <deque>
#include <string_view>
#include <mutex>
#include <memory>
#include <cstring>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <sys/uio.h> 
#include <netinet/in.h>
#include "../kvs/kvs.hpp"
#include "../utils/time.hpp"
#include "../non_copyable.hpp"
#include "coroutines.hpp"
#include "sockutils.hpp"
#include "constants.hpp"
#include "protocol.hpp"

namespace server {
    struct RespTransactionState {
        enum class CommandType : uint8_t { Get, Set, Del };

        struct QueuedCommand {
            CommandType type = CommandType::Get;
            const char* key = nullptr;
            const char* value = nullptr;
        };

        const char* persistString(const char* input) {
            if (!input || !*input) {
                static constexpr char EMPTY[] = "";
                return EMPTY;
            }

            const size_t length = std::strlen(input);
            auto buffer = std::unique_ptr<char[]>(new char[length + 1]);
            std::memcpy(buffer.get(), input, length + 1);
            const char* result = buffer.get();
            storage.emplace_back(std::move(buffer));
            return result;
        }

        void clearQueue() {
            queue.clear();
            storage.clear();
        }

        bool active = false;
        bool aborted = false;
        std::vector<QueuedCommand> queue;
        std::vector<std::unique_ptr<char[]>> storage;
    };

    struct WriteBatch {
        struct Chunk {
            std::size_t offset;
            std::size_t len;
        };

        std::vector<Chunk> chunks;
        std::size_t totalBytes = 0;
    };

    struct ConnectionData {
        private:
            WriteBatch writeBatch;
            std::vector<char> writeStorage;

        public:
            timespec lastActivity {0, 0};
            int epoll_fd = -1;
            std::vector<char> readBuffer;
            std::deque<RequestView> pendingRequests;
            size_t bytesToErase = 0;
            std::unique_ptr<RespTransactionState> respTransaction;

            void queueResponseChunk(const char* data, std::size_t len, RequestProtocol protocol) noexcept
            {
                if (len == 0) {
                    return;
                }

                WriteBatch& wb = writeBatch;

                auto offset = writeStorage.size();
                writeStorage.insert(writeStorage.end(), data, data + len);

                wb.chunks.push_back(WriteBatch::Chunk{offset, len});
                wb.totalBytes += len;


                if (protocol == RequestProtocol::Custom) {
                    const auto sep = MSG_SEPARATOR;
                    std::size_t sepOffset = writeStorage.size();
                    writeStorage.push_back(sep);

                    wb.chunks.push_back(WriteBatch::Chunk{sepOffset, 1});
                    wb.totalBytes += 1;
                }
            }
            
            bool flushWriteBatch(int fd) noexcept
            {
                WriteBatch& wb = writeBatch;

                if (wb.chunks.empty()) {
                    return true;
                }

                std::vector<iovec> iov;
                iov.reserve(wb.chunks.size());
                char* base = writeStorage.data();
                for (const auto& ch : wb.chunks) {
                    iovec v{};
                    v.iov_base = base + ch.offset;
                    v.iov_len  = ch.len;
                    iov.push_back(v);
                }

                msghdr msg{};
                msg.msg_iov    = iov.data();
                msg.msg_iovlen = iov.size();

                auto n = ::sendmsg(fd, &msg, MSG_DONTWAIT | MSG_NOSIGNAL);
                if (n == -1) {
                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        return true;
                    }
                    return false;
                }

                std::size_t written = static_cast<std::size_t>(n);
                if (written < wb.totalBytes) {
                    return false;
                }

                wb.totalBytes = 0;
                wb.chunks.clear();
                writeStorage.clear();
                return true;
            }
            
            ConnectionData() = default;
            ConnectionData(timespec ts, int epfd) : lastActivity(ts), epoll_fd(epfd) {
                readBuffer.reserve(READ_BUFFER_SIZE);
            }
            ~ConnectionData();
    };

    class ConnManager {
        private:
            int epoll_fd;
            std::mutex conn_mutex;

            int registerConnection(int epoll_fd, int client_fd) {
                std::lock_guard<std::mutex> lock(conn_mutex);
#ifndef NDEBUG
                std::cout << "Adding client_fd = " << client_fd << " to epoll_fd = " << epoll_fd << std::endl;
#endif
                setNonBlocking(client_fd);
                epoll_event event{};
                event.events = EPOLLIN | EPOLLET;
                event.data.fd = client_fd;

                if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &event) == -1) {
                    perror("Failed to add client_fd to epoll");
                    closeConnection(client_fd);
                    return -1;
                }
                timespec time{0, 0};
                if (clock_gettime(CLOCK_MONOTONIC_COARSE, &time) == 0) {
                    auto [iterator, success] = connections.try_emplace(client_fd, time, epoll_fd );
                    if (!success) {
#ifndef NDEBUG
                        std::cerr << "Connection info already exists for client_fd = " << client_fd << ", epoll_fd = " << epoll_fd << std::endl;
#endif
                        return 0;
                    }
                } else {
                    perror("clock_gettime() failed when registering connection");
                    return -1;
                }
                return 0; 
            };

            void validateConnections() {
                std::lock_guard<std::mutex> lock(conn_mutex);
                timespec now{0, 0};
                if (clock_gettime(CLOCK_MONOTONIC_COARSE, &now) == 0) {
                    for (auto it = connections.begin(); it != connections.end();) {
                        auto diff = now - it->second.lastActivity;
                        auto fd = it->first;
                        ++it;
                        if (diff.tv_sec > MAX_CONN_LIFETIME_SEC) {
                            closeConnection(fd);
                        }
                    }
                } else {
                    perror("clock_gettime() failed when validating connections");
                }
            };

        public:
            std::unordered_map<int, ConnectionData> connections;

            bool updateActivity(int fd) {
                std::lock_guard<std::mutex> lock(conn_mutex);
                timespec time{0, 0};
                if (clock_gettime(CLOCK_MONOTONIC_COARSE, &time) == 0) {
                    connections[fd].lastActivity = time;
                } else {
                    perror("clock_gettime() failed when updating connection activity");
                    return false;
                }
                return true;
            };

            void closeConnection(int fd) noexcept {
                std::lock_guard<std::mutex> lock(conn_mutex);
                if (!connections.contains(fd)) {
                    return;
                }
                if (epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr) == -1) {
#ifndef NDEBUG
                    perror("Error when removing socket descriptor from epoll");
#endif
                };
                if (shutdown(fd, SHUT_RDWR) == -1) {
#ifndef NDEBUG
                    perror("Error when shutting down socket descriptor");
#endif
                }
                if (close(fd) == -1) {
#ifndef NDEBUG
                    perror("Error when closing socket descriptor");
#endif
                }
                connections.erase(fd);
            };

            AcceptConnTask acceptConnections(int server_fd, std::atomic<bool>& isRunning) {
                sockaddr_in client_address{};
                socklen_t client_len = sizeof(client_address);
                while (isRunning.load(std::memory_order_relaxed)) {
                    int acceptedCount = 0;
                    int lastError = 0;
                    while (isRunning.load(std::memory_order_relaxed)) {
                        auto client_fd = accept(server_fd, (struct sockaddr*)&client_address, &client_len);
                        if (client_fd >= 0) {
                            if (registerConnection(epoll_fd, client_fd) == -1) {
                                continue;
                            }
                            ++acceptedCount;
                            lastError = 0;
                            continue;
                        }

                        lastError = errno;

                        if (!connections.empty()) {
                            validateConnections();
                        }

                        if (lastError == EINTR) {
                            perror("Failed to accept connection: interruption signal received. Retrying...");
                            continue;
                        }
                        if (lastError == EAGAIN || lastError == EWOULDBLOCK) {
                            break;
                        }

                        perror("Failed to accept connection");
                        acceptedCount = -1;
                        break;
                    }

                    co_yield acceptedCount;

                    if (lastError == EAGAIN || lastError == EWOULDBLOCK) {
                        std::this_thread::sleep_for(ACCEPT_CONN_DELAY);
                    }
                }
                co_return 0;
            }

            ConnManager(int epoll_fd): epoll_fd(epoll_fd) {}
    };
}

