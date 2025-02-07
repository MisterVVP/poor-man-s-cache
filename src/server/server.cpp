#include "server.h"

using namespace server;

int_fast8_t ServerShard::processCommand(const Command &command)
{
    const char* response = nullptr;
    int_fast8_t err = 0;
    if (command.commandCode == 1) {
        auto setRes = keyValueStore.set(command.key, command.value, command.hash);
        response = setRes ? "OK" : "ERROR: Internal error";
    } else {
        err = 1;
        response = "ERROR: Invalid command code";
    }

    auto res = send(command.client_fd, response, strlen(response), MSG_DONTWAIT);
    if (res == -1) {
        perror("Error when sending data back to client");
        err = 1;
    }
    return err;
}

int_fast8_t ServerShard::processQuery(const Query &query)
{
    const char* response = nullptr;
    int_fast8_t err = 0;
    if (query.queryCode == 1) {
        const char* value = keyValueStore.get(query.key, query.hash);
        response = value ? value : "(nil)";
    } else {
        err = 1;
        response = "ERROR: Invalid query code";        
    }

    auto res = send(query.client_fd, response, strlen(response), MSG_DONTWAIT);
    if (res == -1) {
        perror("Error when sending data back to client");
        err = 1;
    }
    return err;
}

CacheServer::CacheServer(std::atomic<bool>& cToken, const ServerSettings settings): port(settings.port), numErrors(0), cancellationToken(cToken),
    server_fd(-1), isRunning(false), activeConnectionsCounter(0), numRequests(0), numShards(settings.numShards), trashEmptyFrequency(settings.trashEmptyFrequency)
{

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

int_fast8_t CacheServer::handleRequest()
{
#ifndef NDEBUG
    auto start = std::chrono::high_resolution_clock::now();
    std::cout << "handleRequest() started! epoll_fd = " << epoll_fd << std::endl;
#endif
    int_fast8_t result = 0;
    epoll_event events[MAX_EVENTS];
    int event_count;
    do {
        event_count = epoll_wait(epoll_fd, events, MAX_EVENTS, EPOLL_WAIT_TIMEOUT);
        if (event_count == -1 && errno != EINTR) {
            perror("epoll_wait failed");
            return 1;
        }
    } while (event_count < 0 && errno == EINTR);

    for (auto i = 0; i < event_count; ++i) {
        auto client_fd = events[i].data.fd;
        if (events[i].events & EPOLLIN) {
            std::vector<RequestPart> requestParts{};
            size_t request_size = 0;
            char buffer[READ_BUFFER_SIZE] = {0};
            for (;;) {
                auto bytes_read = read(client_fd, buffer, sizeof(buffer) - 1);
                if (bytes_read == -1) {
                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        break;
                    } else {
                        perror("Failed to read client request buffer");
                        closeConnection(client_fd);
                        return 1;
                    }
                } else if (bytes_read == 0) {
                    closeConnection(client_fd);
                    return 1;
                } else {
                    char* partVal = new char[bytes_read];
                    memcpy(partVal, buffer, bytes_read);
                    requestParts.emplace_back(partVal, bytes_read, request_size);                    
                    request_size += bytes_read;
                    trashcan.AddGarbage(partVal);
                }
            }
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
                    closeConnection(client_fd);
                    trashcan.AddGarbage(request);
                    ++result;
                    continue;
                }
                char* key = strtok_r(nullptr, " ", &com_saveptr);
                if (key) {                        
                    auto hash = hashFunc(key);
                    auto shardId = hash % numShards;

                    auto& shard = serverShards[shardId];
                    if (strcmp(command, "GET") == 0) {
                        auto query = createQuery(1, key, hash, client_fd);
                        result += shard.processQuery(query);
                        closeConnection(client_fd);
                        trashcan.AddGarbage(query.key);
                    } else if (strcmp(command, "SET") == 0) {
                        if (com_saveptr) {
                            auto cmd = createCommand(1, key, com_saveptr, hash, client_fd);
                            result += shard.processCommand(cmd);
                            closeConnection(client_fd);
                            trashcan.AddGarbage(cmd.key);
                            trashcan.AddGarbage(cmd.value);
                        } else {
                            const char* response = "ERROR: Invalid SET command format";
#ifndef NDEBUG
                            std::cerr << response << ", command:" << command << std::endl;
#endif
                            send(client_fd, response, strlen(response), 0);
                            ++result;
                            closeConnection(client_fd);
                        }
                    }
                } else {
                    const char* response = "ERROR: Unknown command";
#ifndef NDEBUG
                    std::cerr << response << ":" << command << std::endl;
#endif
                    ++result;
                    send(client_fd, response, strlen(response), 0);
                    closeConnection(client_fd);
                }
                trashcan.AddGarbage(request);
            } else {
                perror("Failed to read client request buffer");
                closeConnection(client_fd);
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

void CacheServer::closeConnection(int client_fd)
{
    auto sdRes = shutdown(client_fd, SHUT_RDWR);
    if (sdRes == -1) {
        perror("Error when shutting down client_fd");
    }
    auto closeRes = close(client_fd);
    if (closeRes == -1) {
        perror("Error when closing client_fd");
    } else {
        --activeConnectionsCounter;
    }
}

void CacheServer::metricsUpdater(std::queue<CacheServerMetrics>& channel, std::stop_token stopToken)
{
    while (!stopToken.stop_requested()) {
        metricsSemaphore.try_acquire_for(METRICS_UPDATE_FREQUENCY_SEC);
        channel.push(CacheServerMetrics(numErrors, activeConnectionsCounter, numRequests));
    }
}

int CacheServer::Start(std::queue<CacheServerMetrics>& channel)
{
    isRunning = true;
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        perror("Socket creation failed");
        return 1;
    }

    int flag = 1;
    if (setsockopt(server_fd, IPPROTO_TCP, TCP_NODELAY, (void*)&flag, sizeof(flag)) == -1) {
        perror("Failed to set TCP_NODELAY for server socket");
        return 1;
    }
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag)) == -1) {
        perror("Failed to set SO_REUSEADDR for server socket");
        return 1;
    }
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEPORT, &flag, sizeof(flag)) == -1) {
        perror("Failed to set SO_REUSEPORT for server socket");
        return 1;
    }
    int qlen = 5;
    if (setsockopt(server_fd, SOL_TCP, TCP_FASTOPEN, &qlen, sizeof(qlen)) == -1) {
        perror("Failed to set SO_REUSEPORT for server socket");
        return 1;
    }
    if (setNonBlocking(server_fd) == -1) {
        std::cout << "Failed to set O_NONBLOCK for server socket" << std::endl;
        return 1;
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        std::cerr << "Bind failed" << std::endl;
        close(server_fd);
        return 1;
    }

    if (listen(server_fd, CONN_QUEUE_LIMIT) < 0) {
        perror("Listen failed");
        close(server_fd);
        return 1;
    }

    std::cout << "Initializing " << numShards << " server shards..." << std::endl;
    serverShards = std::make_unique<ServerShard[]>(numShards);
    for (int i = 0; i < numShards; ++i) {
        serverShards[i].shardId = i;
    }

    metricsUpdaterThread = std::jthread([this, &channel](std::stop_token stopToken)
    {
        metricsUpdater(channel, stopToken);
    });

    epoll_fd = epoll_create1(0);
    if (epoll_fd == -1) {
        perror("Failed to create epoll instance");
        return 1;
    }
    std::cout << "Server started on port " << port << ", " << numShards << " shards are ready" << std::endl;

    while (!cancellationToken) {
        sockaddr_in client_address;
        socklen_t client_len = sizeof(client_address);
        auto client_fd = accept(server_fd, (struct sockaddr*)&client_address, &client_len);
        if (client_fd >= 0) {
            ++activeConnectionsCounter;
            ++numRequests;
            setNonBlocking(client_fd);
            epoll_event event{};
            event.events = EPOLLIN | EPOLLET;
            event.data.fd = client_fd;
            if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &event) == -1) {
                perror("Failed to add client_fd to epoll");
                closeConnection(client_fd);
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
