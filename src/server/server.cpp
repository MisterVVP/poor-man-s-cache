#include "server.h"

using namespace server;

CacheServer::CacheServer(std::atomic<bool>& cToken, const ServerSettings settings):
    cancellationToken(cToken), numShards(settings.numShards), port(settings.port)
{
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        throw std::system_error(errno, std::system_category(), "Socket creation failed");
    }

    int flag = 1;
    if (setsockopt(server_fd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag)) == -1) {
        close(server_fd);
        throw std::system_error(errno, std::system_category(), "Failed to set TCP_NODELAY for server socket");
    }
    if (setsockopt(server_fd, IPPROTO_TCP, TCP_DEFER_ACCEPT, &flag, sizeof(flag)) == -1) {
        close(server_fd);
        throw std::system_error(errno, std::system_category(), "Failed to set SO_REUSEPORT for server socket");
    }
    if (setsockopt(server_fd, IPPROTO_TCP, TCP_QUICKACK, &flag, sizeof(flag)) == -1) {
        close(server_fd);
        throw std::system_error(errno, std::system_category(), "Failed to set SO_REUSEPORT for server socket");
    }
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag)) == -1) {
        close(server_fd);
        throw std::system_error(errno, std::system_category(), "Failed to set SO_REUSEADDR for server socket");
    }
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEPORT, &flag, sizeof(flag)) == -1) {
        close(server_fd);
        throw std::system_error(errno, std::system_category(), "Failed to set SO_REUSEPORT for server socket");
    }

    int qlen = 2048;
    if (setsockopt(server_fd, SOL_TCP, TCP_FASTOPEN, &qlen, sizeof(qlen)) == -1) {
        close(server_fd);
        throw std::system_error(errno, std::system_category(), "Failed to set TCP_FASTOPEN for server socket");
    }

    if (setSocketBuffers(server_fd, settings.sockBuffer, SOCK_BUF_OPTS::SOCK_BUF_ALL) == -1) {
        close(server_fd);
        throw std::runtime_error("Failed to set socket buffer options for server socket");
    }

    if (setNonBlocking(server_fd) == -1) {
        close(server_fd);
        throw std::runtime_error("Failed to set O_NONBLOCK for server socket");
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        close(server_fd);
        throw std::system_error(errno, std::system_category(), "Bind failed");
    }

    if (listen(server_fd, settings.connQueueLimit) < 0) {
        close(server_fd);
        throw std::system_error(errno, std::system_category(), "Listen failed");
    }

    epoll_fd = epoll_create1(0);
    if (epoll_fd == -1) {
        throw std::system_error(errno, std::system_category(), "Failed to create epoll instance");
    }

    connManager = std::make_unique<ConnManager>(epoll_fd, conn_mutex);

#ifndef NDEBUG
    std::cout << "Initializing " << numShards << " server shards…" << std::endl;
#endif
    serverShards.reserve(numShards);
    KeyValueStoreSettings kvsSettings { 2053, settings.enableCompression, true };
    for (int i = 0; i < numShards; ++i) {
        serverShards.emplace_back(i, kvsSettings);
    }
}

CacheServer::~CacheServer() {
    Stop();
    if (server_fd >= 0) {
        close(server_fd);
    }
    serverShards.clear();
}

ProcessRequestTask server::CacheServer::processRequest(std::string_view requestData, int client_fd)
{
    const char* response = nullptr;

    const auto firstSpace = requestData.find(' ');
    if (firstSpace == std::string_view::npos) {
        ++numErrors;
        response = UNABLE_TO_PARSE_REQUEST_ERROR;
    } else {
        const auto command = requestData.substr(0, firstSpace);
        const auto remainder = requestData.substr(firstSpace + 1);
        const auto secondSpace = remainder.find(' ');

        char* keyPtr = const_cast<char*>(remainder.data());
        const char* valuePtr = nullptr;

        if (secondSpace != std::string_view::npos) {
            keyPtr[secondSpace] = '\0';
            valuePtr = keyPtr + secondSpace + 1;
        }

        auto hash = hashFunc(keyPtr);
        auto shardId = hash % numShards;
        auto& shard = serverShards[shardId];

        if (command == GET_STR) {
            Query query{QueryCode::GET, keyPtr, hash};
            response = shard.processQuery(query);
        }

        if (command == SET_STR) {
            if (valuePtr) {
                Command cmd{CommandCode::SET, keyPtr, valuePtr, hash};
                response = shard.processCommand(cmd);
            } else {
                ++numErrors;
                response = INVALID_COMMAND_FORMAT;
            }
        }

        if (command == DEL_STR) {
            Command cmd{CommandCode::DEL, keyPtr, nullptr, hash};
            response = shard.processCommand(cmd);
        }
    }

    if (!response) {
        ++numErrors;
        response = UNKNOWN_COMMAND;
    }
    auto srt = sendResponse(client_fd, response);
    co_await srt;
}

HandleReqTask CacheServer::handleRequests()
{
    while (!cancellationToken) {
#ifndef NDEBUG
        auto start = std::chrono::high_resolution_clock::now();
#endif

        int event_count = epoll_wait(epoll_fd, epoll_events, MAX_EVENTS, EPOLL_WAIT_TIMEOUT_MSEC);
        if (event_count == -1) {
            if (errno != EINTR) {
                perror("epoll_wait failed");
            }
            co_return event_count;
        } else if (event_count == 0) {
#ifndef NDEBUG
            std::cout << "handleRequests finished without events to handle!" << std::endl;
#endif
            co_yield event_count;
        } else {
            std::vector<AsyncReadTask> readers;
            readers.reserve(MAX_EVENTS);
            numRequests += event_count;
            eventsPerBatch = event_count;
            for (int i = 0; i < event_count; ++i) {
                auto client_fd = epoll_events[i].data.fd;
                if ((epoll_events[i].events & (EPOLLERR | EPOLLHUP))) {
                    connManager->closeConnection(client_fd);
                    continue;
                }

                if (epoll_events[i].events & EPOLLIN) {
                    auto asyncRead = readRequestAsync(client_fd);
                    connManager->updateActivity(client_fd);
                    asyncRead.client_fd = client_fd;
                    readers.emplace_back(std::move(asyncRead));
                }
            }
            std::vector<ProcessRequestTask> requestsToProcess;
            for (int i = 0; i < readers.size(); ++i) {
                const std::lock_guard<std::mutex> lock(conn_mutex);
                auto fd = readers[i].client_fd;
#ifndef NDEBUG
                std::cout << "reading request from client_fd = " << fd  << ", epoll_fd = " << epoll_fd << std::endl;
#endif
                auto readResult = co_await readers[i];
                if (readResult.operationResult == ReqReadOperationResult::Failure) {
                    ++numErrors;
                    continue;
                }

                if (readResult.operationResult == ReqReadOperationResult::AwaitingData) {
                    continue;
                }

                auto& connData = connManager->connections[fd];
                while (!connData.pendingRequests.empty()) {
                    auto req = connData.pendingRequests.front();
                    connData.pendingRequests.pop_front();
                    auto processReqTask = processRequest(req, fd);
                    requestsToProcess.emplace_back(std::move(processReqTask));
                }
                if (connData.bytesToErase > 0) {
                    connData.readBuffer.erase(connData.readBuffer.begin(), connData.readBuffer.begin() + connData.bytesToErase);
                    connData.bytesToErase = 0;
                }
            }

            for (int i = 0; i < requestsToProcess.size(); ++i) {
                co_await requestsToProcess[i];
            }

#ifndef NDEBUG
            auto stop = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(stop - start);
            std::cout << "handleRequests interation finished in " << duration.count() << " ns ! event_count = " << event_count << std::endl;
#endif
            co_yield event_count;
        }
    }
}

AsyncReadTask server::CacheServer::readRequestAsync(int client_fd)
{
    char buffer[READ_BUFFER_SIZE];
    buffer[0] = 0;
    uint16_t readErrorsCounter = 0;
    uint_fast32_t read_attempts = 0;
    bool parsed = false;
    auto& connData = connManager->connections[client_fd];
    while (read_attempts < READ_MAX_ATTEMPTS) {
        ssize_t bytes_read = co_await AsyncReadAwaiter(client_fd, buffer, sizeof(buffer));
        if (bytes_read == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            } else if (errno == EINTR && readErrorsCounter < READ_NUM_RETRY_ON_INT) {
                perror("Failed to read client request buffer: interruption signal received. Retrying…");
                ++readErrorsCounter;
                continue;
            }
            perror("Failed to read client request buffer");
            co_return ReadRequestResult{ ReqReadOperationResult::Failure };
        }

        if (bytes_read == 0) {
            connManager->closeConnection(client_fd);
            co_return ReadRequestResult{ ReqReadOperationResult::Failure };
        }

        connData.readBuffer.insert(connData.readBuffer.end(), buffer, buffer + bytes_read);
        ++read_attempts;
    }

    size_t start = 0;
    for (size_t i = 0; i < connData.readBuffer.size(); ++i) {
        if (connData.readBuffer[i] == MSG_SEPARATOR) {
            size_t len = i - start;
            connData.readBuffer[i] = '\0';
            std::string_view req{connData.readBuffer.data() + start, len};
            connData.pendingRequests.emplace_back(req);
            parsed = true;
            start = i + 1;
        }
    }

    if (start > 0) {
        connData.bytesToErase += start;
    }

    if (parsed) {
        co_return ReadRequestResult{ ReqReadOperationResult::Success };
    }

    co_return ReadRequestResult{ ReqReadOperationResult::AwaitingData };
}

AsyncSendTask CacheServer::sendResponse(int client_fd, const char* response) {
    const auto responseSize = strlen(response);
    char sep = MSG_SEPARATOR;
    struct iovec iov[2];

    iov[0].iov_base = const_cast<char*>(response);
    iov[0].iov_len  = responseSize;
    iov[1].iov_base = &sep;
    iov[1].iov_len  = 1;

    size_t totalRequired = responseSize + 1;
    size_t totalSent = 0;
    int iov_idx = 0;
    
    struct msghdr msg{};
    msg.msg_iov    = iov;
    msg.msg_iovlen = 2;

    while (totalSent < totalRequired) {
        auto bytesSent = co_await AsyncSendAwaiter(client_fd, &msg);

        if (bytesSent == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            } else {
                perror("Error when sending data back to client");
                ++numErrors;
                break;
            }
        }

        totalSent += bytesSent;

        while (bytesSent > 0 && iov_idx < 2) {
            if (static_cast<size_t>(bytesSent) >= iov[iov_idx].iov_len) {
                bytesSent -= iov[iov_idx].iov_len;
                ++iov_idx;
            } else {
                iov[iov_idx].iov_base = static_cast<char*>(iov[iov_idx].iov_base) + bytesSent;
                iov[iov_idx].iov_len -= bytesSent;
                bytesSent = 0;
            }
        }

        msg.msg_iov = &iov[iov_idx];
        msg.msg_iovlen = 2 - iov_idx;
    }
}

void CacheServer::metricsUpdater(std::queue<CacheServerMetrics>& channel, std::stop_token stopToken)
{
    while (!stopToken.stop_requested()) {
        metricsSemaphore.try_acquire_for(METRICS_UPDATE_FREQUENCY_SEC);
        channel.push(CacheServerMetrics(numErrors, connManager->activeConnectionsCounter, numRequests, eventsPerBatch));
    }
}


int CacheServer::Start(std::queue<CacheServerMetrics>& channel)
{
    isRunning = true;
    metricsUpdaterThread = std::jthread([this, &channel](std::stop_token stopToken) {
        metricsUpdater(channel, stopToken);
    });

    std::cout << "Server started on port " << port << ", " << numShards << " shards are ready" << std::endl;

    int resultCode = 0;

    connManagerThread = std::jthread([this](std::stop_token stopToken) {
        connManager->acceptConnections(server_fd);
        shutdownLatch.count_down();
    });

    reqHandlerThread = std::jthread([this](std::stop_token stopToken) {
        auto hrt = handleRequests();
        while (!stopToken.stop_requested() && !cancellationToken) {
            auto events_processed = hrt.next_value();
            if (!events_processed) {
                std::this_thread::sleep_for(PROCESS_REQ_DELAY);
            }
            // TODO: try to recover when events_processed = -1
        }
        shutdownLatch.count_down();
    });

    shutdownLatch.arrive_and_wait();

    Stop();
    
    return resultCode;
}

void CacheServer::Stop() noexcept
{
    if (!isRunning) {
        return;
    }

    std::cout << "Stopping server…\n";
    connManager->stop();
    isRunning        = false;
    cancellationToken = true;

    if (epoll_fd >= 0) {
        close(epoll_fd);
    }

    for (auto* t : { &metricsUpdaterThread, &connManagerThread, &reqHandlerThread }) {
        if (t->joinable()) {
            t->request_stop();
        }
    }

    std::cout << "Server stopped.\n";
}