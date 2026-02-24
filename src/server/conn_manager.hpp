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
#include "../metrics/metrics.hpp"
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
            bool writeInterestEnabled = false;

            void updateEpollWriteInterest(int fd, bool enable) noexcept
            {
                if (epoll_fd < 0 || writeInterestEnabled == enable) {
                    return;
                }

                epoll_event event{};
                event.data.fd = fd;
                event.events = EPOLLIN | EPOLLET | (enable ? EPOLLOUT : 0);

                if (epoll_ctl(epoll_fd, EPOLL_CTL_MOD, fd, &event) == -1) {
#ifndef NDEBUG
                    perror("Failed to update epoll events for connection");
#endif
                    return;
                }

                writeInterestEnabled = enable;
            }

        public:
            timespec lastActivity {0, 0};
            int epoll_fd = -1;
            std::vector<char> readBuffer;
            std::deque<RequestView> pendingRequests;
            size_t bytesToErase = 0;
            std::unique_ptr<RespTransactionState> respTransaction;
            metrics::MetricsCollector* metrics = nullptr;

            void queueResponseChunk(const char* data, std::size_t len, RequestProtocol protocol) noexcept
            {
                if (len == 0) {
                    return;
                }

                WriteBatch& wb = writeBatch;

                auto offset = writeStorage.size();
                writeStorage.insert(writeStorage.end(), data, data + len);

                wb.chunks.push_back({offset, len});
                wb.totalBytes += len;


                if (protocol == RequestProtocol::Custom) {
                    const auto sep = MSG_SEPARATOR;
                    std::size_t sepOffset = writeStorage.size();
                    writeStorage.push_back(sep);

                    wb.chunks.push_back({sepOffset, 1});
                    wb.totalBytes += 1;
                }
            }
            
            bool flushWriteBatch(int fd) noexcept
            {
                WriteBatch& wb = writeBatch;

                if (wb.chunks.empty()) {
                    updateEpollWriteInterest(fd, false);
                    return true;
                }

                char* base = writeStorage.data();
                std::size_t remaining = wb.totalBytes;

                std::size_t idx = 0;
                std::size_t offsetInside = 0;
                bool needMoreWrite = false;

                while (remaining > 0 && idx < wb.chunks.size()) {

                    std::vector<iovec> iov;
                    iov.reserve(wb.chunks.size() - idx);

                    {
                        const auto& first = wb.chunks[idx];
                        iovec chunkPartV{};
                        chunkPartV.iov_base = base + first.offset + offsetInside;
                        chunkPartV.iov_len  = first.len - offsetInside;
                        iov.push_back(chunkPartV);
                    }

                    for (std::size_t c = idx + 1; c < wb.chunks.size(); c++) {
                        const auto& ch = wb.chunks[c];
                        iovec chunkV{};
                        chunkV.iov_base = base + ch.offset;
                        chunkV.iov_len  = ch.len;
                        iov.push_back(chunkV);
                    }

                    msghdr msg{};
                    msg.msg_iov = iov.data();
                    msg.msg_iovlen = iov.size();

                    auto bytesSent = ::sendmsg(fd, &msg, MSG_NOSIGNAL | MSG_DONTWAIT);
                    if (bytesSent == -1) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK) {
                            needMoreWrite = true;
                            break;
                        }
                        return false;
                    }

                    auto written = static_cast<std::size_t>(bytesSent);
                    if (written == 0) {
                        needMoreWrite = true;
                        break;
                    }

                    remaining -= written;

                    if (metrics) {
                        metrics->addBytesTx(written);
                    }

                    while (written > 0) {
                        const auto& ch = wb.chunks[idx];
                        auto chunkRemaining = ch.len - offsetInside;

                        if (written >= chunkRemaining) {
                            written -= chunkRemaining;
                            idx++;
                            offsetInside = 0;
                            if (idx >= wb.chunks.size())
                                break;
                        } else {
                            offsetInside += written;
                            written = 0;
                        }
                    }
                }

                if (remaining == 0 && !needMoreWrite) {
                    wb.totalBytes = 0;
                    wb.chunks.clear();
                    writeStorage.clear();
                    updateEpollWriteInterest(fd, false);
                    return true;
                }

                if (idx < wb.chunks.size()) {
                    const auto dropBytes = wb.chunks[idx].offset + offsetInside;
                    if (dropBytes > 0 && dropBytes <= writeStorage.size()) {
                        writeStorage.erase(writeStorage.begin(), writeStorage.begin() + dropBytes);
                    }

                    std::vector<WriteBatch::Chunk> newChunks;
                    newChunks.reserve(wb.chunks.size() - idx);

                    auto firstLen = wb.chunks[idx].len > offsetInside ? (wb.chunks[idx].len - offsetInside) : 0;
                    if (firstLen > 0) {
                        newChunks.push_back({0, firstLen});
                    }

                    for (std::size_t c = idx + 1; c < wb.chunks.size(); ++c) {
                        auto ch = wb.chunks[c];
                        ch.offset -= dropBytes;
                        newChunks.push_back(ch);
                    }

                    wb.chunks.swap(newChunks);
                    wb.totalBytes = remaining;
                }

                updateEpollWriteInterest(fd, true);
                return true;
            }

            ConnectionData() = default;
            ConnectionData(timespec ts, int epfd, metrics::MetricsCollector* metricsCollector = nullptr)
                : lastActivity(ts), epoll_fd(epfd), metrics(metricsCollector) {
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
                    auto [iterator, success] = connections.try_emplace(client_fd, time, epoll_fd, metrics);
                    if (!success) {
#ifndef NDEBUG
                        std::cerr << "Connection info already exists for client_fd = " << client_fd << ", epoll_fd = " << epoll_fd << std::endl;
#endif
                        return 0;
                    }
                    if (metrics) {
                        metrics->connectionAccepted();
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

            void closeConnection(int fd, metrics::CloseReason reason = metrics::CloseReason::Server) noexcept {
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
                if (metrics) {
                    metrics->connectionClosed(reason);
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
                        if (metrics) {
                            metrics->incrementSyscallAccept();
                        }
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

                        if ((lastError == EBADF || lastError == EINVAL) && !isRunning.load(std::memory_order_acquire)) {
                            // Stop() may close the listen socket while accept loop is winding down.
                            // Treat this as a graceful shutdown signal rather than a fatal accept error.
                            lastError = 0;
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

            ConnManager(int epoll_fd, metrics::MetricsCollector* metrics = nullptr): epoll_fd(epoll_fd), metrics(metrics) {}

            void setMetrics(metrics::MetricsCollector* m) noexcept { metrics = m; }

        private:
            metrics::MetricsCollector* metrics = nullptr;
    };
}
