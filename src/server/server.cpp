#include "server.h"

using namespace server;

CacheServer::CacheServer(std::atomic<bool>& cToken, const ServerSettings settings):
    numErrors(0), cancellationToken(cToken), isRunning(false), numRequests(0), numShards(settings.numShards),
    trashEmptyFrequency(settings.trashEmptyFrequency), port(settings.port), connManager()
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

    epoll_fd = epoll_create1(0);
    if (epoll_fd == -1) {
        close(server_fd);
        throw std::system_error(errno, std::system_category(), "Failed to create epoll instance");
    }

#ifndef NDEBUG
    std::cout << "Initializing " << numShards << " server shards..." << std::endl;
#endif
    serverShards.reserve(numShards);
    KeyValueStoreSettings kvsSettings { 2053, settings.enableCompression, true, true };
    for (int i = 0; i < numShards; ++i) {
        serverShards.emplace_back(i, kvsSettings);
    }
}

CacheServer::~CacheServer() {
    Stop();
    if (server_fd >= 0) {
        close(server_fd);
    }
    if (epoll_fd >= 0) {
        close(epoll_fd);
    }
}

Command CacheServer::createCommand(uint_fast16_t code, char *key, char *value, uint_fast64_t hash, int client_fd) const
{
    Command cmd{1, nullptr, nullptr, hash, client_fd};

    auto vSize = strlen(value) + 1;
    cmd.value = new char[vSize];
    memcpy(cmd.value, value, vSize);
    cmd.value[vSize-1] = '\0';

    auto kSize = strlen(key) + 1;
    cmd.key = new char[kSize];
    memcpy(cmd.key, key, kSize);
    cmd.key[kSize-1] = '\0';

    return cmd;
}

Query CacheServer::createQuery(uint_fast16_t code, char *key, uint_fast64_t hash, int client_fd) const
{
    Query query{code, nullptr, hash, client_fd};

    auto kSize = strlen(key) + 1;
    query.key = new char[kSize];
    memcpy(query.key, key, kSize);
    query.key[kSize-1] = '\0';

    return query;
}

size_t server::CacheServer::readRequest(int client_fd, std::vector<RequestPart> &requestParts)
{
    size_t request_size = 0;
    char buffer[READ_BUFFER_SIZE];
    buffer[0] = 0;
    uint16_t readErrorsCounter = 0;
    bool receivedLastBlock = false;
    uint_fast32_t read_attempts = 0;
    while (!receivedLastBlock && read_attempts < READ_MAX_ATTEMPTS) {
        auto bytes_read = read(client_fd, buffer, sizeof(buffer) - 1);
        if (bytes_read == -1) {
            readErrorsCounter++;
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            } else {
                if (errno == EINTR && readErrorsCounter < READ_NUM_RETRY_ON_INT) {
                    perror("Failed to read client request buffer: interruption signal received. Retrying...");
                    continue;
                }
                perror("Failed to read client request buffer");
                connManager.closeConnection(client_fd);
                break;
            }
        } else if (bytes_read == 0) {
            connManager.closeConnection(client_fd);            
            break;
        } else {
            if (buffer[bytes_read - 1] == MSG_SEPARATOR) {
                receivedLastBlock = true;
                --bytes_read;
            }
            char* partVal = new char[bytes_read];
            memcpy(partVal, buffer, bytes_read);
            requestParts.emplace_back(partVal, bytes_read, request_size);                    
            request_size += bytes_read;
            trashcan.AddGarbage(partVal);
        }
        ++read_attempts;
    }
    return request_size;
}

int_fast8_t CacheServer::handleRequest()
{
#ifndef NDEBUG
    auto start = std::chrono::high_resolution_clock::now();
    std::cout << "handleRequest() started! epoll_fd = " << epoll_fd << std::endl;
#endif
    int_fast8_t result = 0;
    int event_count;
    do {
        event_count = epoll_wait(epoll_fd, epoll_events, MAX_EVENTS, EPOLL_WAIT_TIMEOUT);
        if (event_count == -1 && errno != EINTR) {
            perror("epoll_wait failed");
            return 1;
        }
    } while (event_count < 0 && errno == EINTR);

    for (auto i = 0; i < event_count; ++i) {
        auto client_fd = epoll_events[i].data.fd;
        if ((epoll_events[i].events & EPOLLERR) || (epoll_events[i].events & EPOLLHUP)) {
            perror("Failed to process client request - epoll error");
            connManager.closeConnection(client_fd);
            continue;
        }
        if (epoll_events[i].events & EPOLLIN) {
            std::vector<RequestPart> requestParts{};
            auto request_size = readRequest(client_fd, requestParts);
            auto request = new char[request_size + 1];
            for (auto i = 0; i < requestParts.size(); ++i) {    
                memcpy(request + requestParts[i].location, requestParts[i].part, requestParts[i].size);
            }
            request[request_size] = '\0';
            if (request) {
                char* com_saveptr = nullptr;
                char* command = strtok_r(request, " ", &com_saveptr);
                if (command == NULL) {
                    const char* response = "ERROR: Unable to parse command";
                    send(client_fd, response, strlen(response), 0);
                    connManager.closeConnection(client_fd);
                    trashcan.AddGarbage(request);
                    ++result;
                    continue;
                }
                char* key = strtok_r(nullptr, " ", &com_saveptr);
#ifndef NDEBUG
                std::cout<< "request key = " << key << ", request_size = " << request_size << ", requestParts.size() = " << requestParts.size() << std::endl;
#endif
                if (key) {
                    auto hash = hashFunc(key);
                    auto shardId = hash % numShards;
                    auto& shard = serverShards[shardId];
                    if (strcmp(command, "GET") == 0) {
                        auto query = createQuery(1, key, hash, client_fd);
                        auto response = shard.processQuery(query);
                        auto responseSize = strlen(response);
                        // TODO: Think how to improve this criteria
                        if (responseSize > ASYNC_RESPONSE_SIZE_THRESHOLD) {
#ifndef NDEBUG
                            std::cout << "Sending async response for query.client_fd = " << query.client_fd << ", query.key = " << query.key << std::endl;
#endif
                            auto aq = std::async(std::launch::async, &CacheServer::sendResponse, this, client_fd, response, responseSize);                                
                            // TODO: more things should be considered or implemented:
                            // 1. error counting for async responses
                            // 2. tracking mechanism for asynch responses
                        } else {
                            result += sendResponse(query.client_fd, response, responseSize);
                        }

                        trashcan.AddGarbage(query.key);
                    } else if (strcmp(command, "SET") == 0) {
                        if (com_saveptr) {
                            auto cmd = createCommand(1, key, com_saveptr, hash, client_fd);
                            auto response = shard.processCommand(cmd);
                            result += sendResponse(client_fd, response, strlen(response));
                            trashcan.AddGarbage(cmd.key);
                            trashcan.AddGarbage(cmd.value);
                        } else {
                            const char* response = "ERROR: Invalid SET command format";
#ifndef NDEBUG
                            std::cerr << response << ", command:" << command << std::endl;
#endif
                            send(client_fd, response, strlen(response), 0);
                            ++result;
                            connManager.closeConnection(client_fd);
                        }
                    }
                } else {
                    const char* response = "ERROR: Unknown command";
#ifndef NDEBUG
                    std::cerr << response << ":" << command << std::endl;
#endif
                    ++result;
                    send(client_fd, response, strlen(response), 0);
                    connManager.closeConnection(client_fd);
                }
                trashcan.AddGarbage(request);
            } else {
                perror("Failed to read client request buffer");
                connManager.closeConnection(client_fd);
                ++result;
            }
        }
    }
#ifndef NDEBUG
    auto stop = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(stop - start);
    std::cout << "handleRequest() finished in " << duration.count() << " μs ! epoll_fd = " << epoll_fd << std::endl;
#endif
    return result;
}

int_fast8_t server::CacheServer::sendResponse(int client_fd, const char* response, const size_t responseSize)
{
    const size_t responseWithSeparatorSize = responseSize + 1;
    char* responseWithSeparator = new char[responseWithSeparatorSize];
    memcpy(responseWithSeparator, response, responseSize);
    responseWithSeparator[responseSize] = MSG_SEPARATOR;

    int_fast8_t errCount = 0;
    size_t totalSent = 0;
    while (totalSent < responseWithSeparatorSize) {
        ssize_t bytesSent = send(client_fd, responseWithSeparator + totalSent, responseWithSeparatorSize - totalSent, 0);

        if (bytesSent == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                std::this_thread::yield();
                continue;
            } else {
                perror("Error when sending data back to client");
                errCount++;
                break;
            }
        }

        totalSent += bytesSent;
    }

    connManager.closeConnection(client_fd);
    delete[] responseWithSeparator;
    return errCount;
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

    while (!cancellationToken) {
        auto client_fd = connManager.acceptConnection(server_fd);
        if (client_fd >= 0) {
            ++numRequests;
            setNonBlocking(client_fd);            
            epoll_event event{};
            event.events = EPOLLIN | EPOLLET;
            event.data.fd = client_fd;
            if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &event) == -1) {
                perror("Failed to add client_fd to epoll");
                connManager.closeConnection(client_fd);
            } else {
                numErrors += handleRequest();
            }
        } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
            perror("Failed to accept connection");
        }
        if (numRequests % trashEmptyFrequency) {
            trashcan.Empty();
        }
    }

    std::cout << "Shutting down gracefully..." << std::endl;
    Stop();
    std::cout << "Server shut down successfully" << std::endl;
    return 0;    
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
