#include "server.h"

using namespace server;

CacheServer::CacheServer(std::atomic<bool>& cToken, const ServerSettings settings):
    cancellationToken(cToken), numShards(settings.numShards), port(settings.port), connManager(cToken)
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

    if (listen(server_fd, settings.connQueueLimit) < 0) {
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

ProcessRequestTask server::CacheServer::processRequest(char *requestData, int client_fd)
{
    char* com_saveptr = nullptr;
    char* request = strtok_r(requestData, " ", &com_saveptr);
    const char* response;
    if (!request) {
        ++numErrors;
        response = UNABLE_TO_PARSE_REQUEST_ERROR;
    }

    char* key = strtok_r(nullptr, " ", &com_saveptr);
    if (key) {
        auto hash = hashFunc(key);
        auto shardId = hash % numShards;
        auto& shard = serverShards[shardId];

        if (strcmp(request, "GET") == 0) {
            Query query {QueryCode::GET, key, hash };
            response = shard.processQuery(query);
        }

        if (strcmp(request, "SET") == 0) {
            if (com_saveptr) {
                Command cmd {CommandCode::SET, key, com_saveptr, hash};
                response = shard.processCommand(cmd);
            } else {
                ++numErrors;
                response = INVALID_COMMAND_FORMAT;
            }
        }

        if (strcmp(request, "DEL") == 0) {
            Command cmd {CommandCode::DEL, key, nullptr, hash};
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

HandleReqTask CacheServer::handleRequests(int epoll_fd)
{
    while (!cancellationToken) {

        #ifndef NDEBUG
        auto start = std::chrono::high_resolution_clock::now();
        std::cout << "handleRequests started! epoll_fd = " << epoll_fd << std::endl;
        #endif

        int event_count = epoll_wait(epoll_fd, epoll_events, MAX_EVENTS, EPOLL_WAIT_TIMEOUT_MSEC);
        if (event_count == -1) {
            if (errno != EINTR) {
                perror("epoll_wait failed");
            }
            co_return;
        } else if (event_count == 0) {
            #ifndef NDEBUG
            std::cout << "handleRequests finished without events to handle!" << std::endl;
            #endif
            co_return;
        }

        numRequests += event_count;
        eventsPerBatch = event_count;
        std::vector<AsyncReadTask> readers;
        for (int i = 0; i < event_count; ++i) {
            auto client_fd = epoll_events[i].data.fd;
            if ((epoll_events[i].events & (EPOLLERR | EPOLLHUP))) {
                connManager.closeConnection(client_fd);
                continue;
            }

            if (epoll_events[i].events & EPOLLIN) {
                #ifndef NDEBUG
                std::cout << "readRequest client_fd = " << client_fd  << ", epoll_fd = " << epoll_fd << std::endl;
                #endif

                auto asyncRead = readRequestAsync(client_fd);
                connManager.updateActivity(client_fd);
                asyncRead.client_fd = client_fd;
                readers.emplace_back(std::move(asyncRead));
            }
        }

        std::vector<ProcessRequestTask> requestsToProcess;
        for (int i = 0; i < readers.size(); ++i) {
            auto fd = readers[i].client_fd;
            auto readResult = co_await readers[i];
            if (readResult.value().operationResult == ReqReadOperationResult::Failure) {
                ++numErrors;
                continue;
            }

            if (readResult.value().operationResult == ReqReadOperationResult::AwaitingData) {
                continue;
            }

            auto processReqTask = processRequest(readResult.value().request.data(), fd);
            requestsToProcess.emplace_back(std::move(processReqTask));
        }

        for (int i = 0; i < requestsToProcess.size(); ++i) {
            co_await requestsToProcess[i];
        }

        #ifndef NDEBUG
        auto stop = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(stop - start);
        std::cout << "handleRequests finished in " << duration.count() << " ns ! event_count = " << event_count << std::endl;
        #endif

        co_return;
    }
}

AsyncReadTask server::CacheServer::readRequestAsync(int client_fd)
{
    char buffer[READ_BUFFER_SIZE];
    buffer[0] = 0;
    std::string request;
    request.reserve(READ_BUFFER_SIZE);
    uint16_t readErrorsCounter = 0;
    bool receivedLastBlock = false;
    uint_fast32_t read_attempts = 0;

    while (!receivedLastBlock && read_attempts < READ_MAX_ATTEMPTS) {
        ssize_t bytes_read = co_await AsyncReadAwaiter(client_fd, buffer, sizeof(buffer));
        if (bytes_read == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            } else if (errno == EINTR && readErrorsCounter < READ_NUM_RETRY_ON_INT) {
                perror("Failed to read client request buffer: interruption signal received. Retrying...");
                ++readErrorsCounter;
                continue;
            }

            perror("Failed to read client request buffer, client_fd = " + client_fd);
            co_return ReadRequestResult::Failure();
        }

        if (bytes_read == 0) {
            co_return ReadRequestResult::AwaitingData();
        }

        if (buffer[bytes_read - 1] == MSG_SEPARATOR) {
            receivedLastBlock = true;
            --bytes_read;
        }
        buffer[bytes_read] = '\0';
        request.append(buffer);
        ++read_attempts;
    }

    if (receivedLastBlock) {
        co_return ReadRequestResult::Success(request);
    }
    co_return ReadRequestResult::Failure();
}

AsyncSendTask server::CacheServer::sendResponse(int client_fd, const char* response)
{
    size_t totalSent = 0;
    auto responseSize = strlen(response);
    const size_t responseWithSeparatorSize = responseSize + 1;
    auto responseWithSeparator = std::make_unique_for_overwrite<char[]>(responseWithSeparatorSize);
    memcpy(responseWithSeparator.get(), response, responseWithSeparatorSize);
    responseWithSeparator.get()[responseSize] = MSG_SEPARATOR;

    while (totalSent < responseWithSeparatorSize) {
        ssize_t bytesSent = co_await AsyncSendAwaiter(client_fd, responseWithSeparator.get() + totalSent, responseWithSeparatorSize - totalSent);

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
    }

    responseWithSeparator.reset();
}

void CacheServer::metricsUpdater(std::queue<CacheServerMetrics>& channel, std::stop_token stopToken)
{
    while (!stopToken.stop_requested()) {
        metricsSemaphore.try_acquire_for(METRICS_UPDATE_FREQUENCY_SEC);
        channel.push(CacheServerMetrics(numErrors, connManager.activeConnectionsCounter, numRequests, eventsPerBatch));
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
    

    std::atomic<int> executionResult;
    std::vector<std::jthread> loopWorkers;
    const auto numThreads = 1; // TODO: right now we are not thread safe, investigate if multithreading could make sense std::min(NUM_WORKERS, std::thread::hardware_concurrency());
    for (int i = 0; i < numThreads; ++i) {
        loopWorkers.emplace_back(std::jthread([this, &executionResult](std::stop_token stopToken)
        {
            auto res = eventLoop();
            if(executionResult != ServerStatus::Stopped) {
                executionResult = res;
            }
        }));
    }

    for (int i = 0; i < numThreads; ++i) {
        loopWorkers[i].join();
    };

    Stop();
    
    return executionResult;
}

EventLoop server::CacheServer::eventLoopIteration(AcceptConnTask& ac)
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

int CacheServer::eventLoop() {
    auto ac = connManager.acceptConnections(server_fd);
    int rCode = 0;
    do {
        auto loop = eventLoopIteration(ac);
        rCode = loop.finalResult();
    } while(!cancellationToken && rCode > 0);

    return rCode;
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
