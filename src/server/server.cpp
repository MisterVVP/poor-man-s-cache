#include "server.h"

using namespace server;

CacheServer::CacheServer(std::atomic<bool>& cToken, const ServerSettings settings):
    numErrors(0), cancellationToken(cToken), isRunning(false), numRequests(0), numShards(settings.numShards),
    port(settings.port), connManager(cToken)
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
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag)) == -1) {
        close(server_fd);
        throw std::system_error(errno, std::system_category(), "Failed to set SO_REUSEADDR for server socket");
    }
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEPORT, &flag, sizeof(flag)) == -1) {
        close(server_fd);
        throw std::system_error(errno, std::system_category(), "Failed to set SO_REUSEPORT for server socket");
    }
    
    int qlen = 5;
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

    if (listen(server_fd, CONN_QUEUE_LIMIT) < 0) {
        close(server_fd);
        throw std::system_error(errno, std::system_category(), "Listen failed");
    }

#ifndef NDEBUG
    std::cout << "Initializing " << numShards << " server shards..." << std::endl;
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

CacheServer::ReadRequestResult server::CacheServer::readRequest(int client_fd)
{
    char buffer[READ_BUFFER_SIZE];
    buffer[0] = 0;
    std::string request;
    request.reserve(READ_BUFFER_SIZE);
    uint16_t readErrorsCounter = 0;
    bool receivedLastBlock = false;
    uint_fast32_t read_attempts = 0;
    while (!receivedLastBlock && read_attempts < READ_MAX_ATTEMPTS) {
        auto bytes_read = read(client_fd, buffer, sizeof(buffer));
        if (bytes_read == -1) {
            readErrorsCounter++;
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            } else if (errno == EINTR && readErrorsCounter < READ_NUM_RETRY_ON_INT) {
                perror("Failed to read client request buffer: interruption signal received. Retrying...");
                continue;
            }
            perror("Failed to read client request buffer");
            return ReadRequestResult::Failure();
        } 
        
        if (bytes_read == 0) {
            return ReadRequestResult::Failure();
        }

        if (buffer[bytes_read - 1] == MSG_SEPARATOR) {
            receivedLastBlock = true;
            --bytes_read;
        }
        buffer[bytes_read] = '\0';
        request += buffer;
        ++read_attempts;
    }
    return ReadRequestResult::Success(request);
}

HandleReqTask CacheServer::handleRequests(int epoll_fd)
{
    int event_count = epoll_wait(epoll_fd, epoll_events, MAX_EVENTS, EPOLL_WAIT_TIMEOUT);
    if (event_count == -1 && errno != EINTR) {
        perror("epoll_wait failed");
        co_return;
    }
    ++numRequests;

    for (int i = 0; i < event_count; ++i) {
        auto client_fd = epoll_events[i].data.fd;
        if ((epoll_events[i].events & (EPOLLERR | EPOLLHUP))) {
            connManager.closeConnection(client_fd);
            continue;
        }

        if (epoll_events[i].events & EPOLLIN) {
            auto readResult = readRequest(client_fd);

            if (readResult.operationResult == ReqReadOperationResult::Failure) {
                connManager.closeConnection(client_fd);
                ++numErrors;
                continue;
            }

            char* com_saveptr = nullptr;
            char* command = strtok_r(readResult.request.data(), " ", &com_saveptr);

            if (!command) {
                const char* response = "ERROR: Unable to parse command";
                send(client_fd, response, strlen(response), 0);
                connManager.closeConnection(client_fd);
                readResult.request.clear();
                ++numErrors;
                continue;
            }
            char* key = strtok_r(nullptr, " ", &com_saveptr);
            if (key) {
                auto hash = hashFunc(key);
                auto shardId = hash % numShards;
                auto& shard = serverShards[shardId];
                if (strcmp(command, "GET") == 0) {
                    Query query {1, key, hash, client_fd };
                    auto response = shard.processQuery(query);
                    readResult.request.clear();
                    auto responseSize = strlen(response);
                    if (responseSize > ASYNC_RESPONSE_SIZE_THRESHOLD) {
                        auto aq = std::async(std::launch::async, &CacheServer::sendResponse, this, client_fd, response, responseSize);
                    } else {
                        sendResponse(query.client_fd, response, responseSize);
                    }
                } else if (strcmp(command, "SET") == 0) {
                    if (com_saveptr) {
                        Command cmd {1, key, com_saveptr, hash, client_fd};
                        auto response = shard.processCommand(cmd);
                        sendResponse(client_fd, response, strlen(response));
                        readResult.request.clear();
                    } else {
                        const char* response = "ERROR: Invalid SET command format";
                        send(client_fd, response, strlen(response), 0);
                        readResult.request.clear();
                        connManager.closeConnection(client_fd);
                        ++numErrors;
                    }
                }
            } else {
                const char* response = "ERROR: Unknown command";
                send(client_fd, response, strlen(response), 0);
                connManager.closeConnection(client_fd);
                readResult.request.clear();
                ++numErrors;
            }
        }
    }
    
    co_return;
}

void server::CacheServer::sendResponse(int client_fd, const char* response, const size_t responseSize)
{
    const size_t responseWithSeparatorSize = responseSize + 1;
    auto responseWithSeparator = std::make_unique_for_overwrite<char[]>(responseWithSeparatorSize);
    memcpy(responseWithSeparator.get(), response, responseWithSeparatorSize);
    responseWithSeparator.get()[responseSize] = MSG_SEPARATOR;

    size_t totalSent = 0;
    while (totalSent < responseWithSeparatorSize) {
        ssize_t bytesSent = send(client_fd, responseWithSeparator.get() + totalSent, responseWithSeparatorSize - totalSent, 0);

        if (bytesSent == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                std::this_thread::yield();
                continue;
            } else {
                perror("Error when sending data back to client");
                ++numErrors;
                break;
            }
        }

        totalSent += bytesSent;
    }

    connManager.closeConnection(client_fd);
    responseWithSeparator.reset();
}

void CacheServer::metricsUpdater(std::queue<CacheServerMetrics>& channel, std::stop_token stopToken)
{
    while (!stopToken.stop_requested()) {
        metricsSemaphore.try_acquire_for(METRICS_UPDATE_FREQUENCY_SEC);
        channel.push(CacheServerMetrics(numErrors, connManager.activeConnectionsCounter, numRequests));
    }
}


int CacheServer::Start(std::queue<CacheServerMetrics>& channel)
{
    isRunning = true;
    metricsUpdaterThread = std::jthread([this, &channel](std::stop_token stopToken)
    {
        metricsUpdater(channel, stopToken);
    });

    std::cout << "Server started on port " << port << ", " << numShards << " shards are ready" << std::endl;

    auto ac = connManager.acceptConnections(server_fd);
    int rCode = 0;
    do {
        auto loopRes = eventLoop(ac);
        rCode = loopRes.finalResult();
    } while(!cancellationToken && rCode > 0);

    if (rCode < 0) {
        std::cerr << "Unexpected termination of the server!" << std::endl;
    }

    Stop();
    
    return rCode;
}

EventLoop server::CacheServer::eventLoop(AcceptConnTask& ac)
{
    auto eStatus = co_await ac;
    
    if (eStatus.status == ServerStatus::Processing) {
        auto hrt = handleRequests(eStatus.epoll_fd);
        co_await hrt;
    }

    if (eStatus.status < 0 ) {
        std::cerr << "Unexpected termination of the server!" << std::endl;
        Stop();
    }

    co_return eStatus.status;
}

void CacheServer::Stop() noexcept
{
    if(isRunning) {
        isRunning = false;

        std::cout << "Stopping server..." << std::endl;
        cancellationToken = true;

        std::cout << "Stopping metrics updater thread..." << std::endl;
        auto mu_sSource =  metricsUpdaterThread.get_stop_source();
        if (mu_sSource.stop_possible()) {
            mu_sSource.request_stop();
        }
        std::cout << "Metrics updater thread stopped" << std::endl;
        std::cout << "Server stopped" << std::endl;
    }
}
