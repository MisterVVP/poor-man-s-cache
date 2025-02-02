#include "server.h"

int_fast8_t CacheServer::processCommand(const Command &command, KeyValueStore &keyValueStore)
{
    const char* response = nullptr;
    int_fast8_t result = 0;
    if (command.commandCode == 1) {
        auto setRes = keyValueStore.set(command.key, command.value, command.hash);
        if (setRes) {
            result ^= 1;
            response = "OK";
        } else {
            response = "ERROR: Internal error";
        }
    } else {
        response = "ERROR: Invalid command code";
    }

    send(command.client_fd, response, strlen(response), 0);
    closeConnection(command.client_fd);

    return result;
}

int_fast8_t CacheServer::processQuery(const Query &query, KeyValueStore &keyValueStore)
{
    const char* response = nullptr;
    int_fast8_t result = 0;
    if (query.queryCode == 1) {
        const char* value = keyValueStore.get(query.key, query.hash);
        if (value) {
            response = value;
            result ^= 1;
        } else {
            response =  "(nil)";
        }
    } else {
        response = "ERROR: Invalid query code";        
    }

    send(query.client_fd, response, strlen(response), 0);
    closeConnection(query.client_fd);

    return result;
}

CacheServer::CacheServer(int port, std::atomic<bool>& cToken): port(port), cancellationToken(cToken), numErrors(0),
    server_fd(-1), isRunning(false), activeConnections(0), numRequests(0)
{
}

CacheServer::~CacheServer() {
    Stop();
    if (server_fd >= 0) {
        close(server_fd);
    }
    for (int epoll_fd : epollInstances) {
        if (epoll_fd >= 0) {
            close(epoll_fd);
        }
    }
}

void CacheServer::workerLoop(ServerShard& shard, std::stop_token stopToken) {
    std::cout << "Started worker loop for shard = " << shard.shardId << std::endl;
    while (!stopToken.stop_requested()) {
        shard.in_semaphore.acquire();
        while(!shard.commandQueue.empty()) {
            auto cmd = shard.commandQueue.front();
            shard.commandQueue.pop();
            numErrors += processCommand(cmd, shard.keyValueStore);
            delete[] cmd.key;
            delete[] cmd.value;
        }

        while(!shard.queryQueue.empty()) {
            auto query = shard.queryQueue.front();
            shard.queryQueue.pop();
            numErrors += processQuery(query, shard.keyValueStore);
            delete[] query.key;
        }
        shard.out_semaphore.release();
    }
    std::cout << "Exited worker loop for shard = " << shard.shardId << std::endl;
}

void CacheServer::epollLoop(int epoll_fd, std::stop_token stopToken)
{
    std::cout << "Started epoll loop for epoll_fd = " << epoll_fd << std::endl;
    epoll_event events[MAX_EVENTS];
    while (!stopToken.stop_requested()) {
        int event_count = epoll_wait(epoll_fd, events, MAX_EVENTS, EPOLL_WAIT_TIMEOUT);
        if (event_count == -1) {
            if (errno == EINTR) continue;
            perror("epoll_wait failed");
            break;
        }

        for (int i = 0; i < event_count; ++i) {
            int client_fd = events[i].data.fd;
            if (events[i].events & EPOLLIN) {
                char buffer[READ_BUFFER_SIZE] = {0};
                int bytes_read = read(client_fd, buffer, sizeof(buffer) - 1);
                if (bytes_read > 0) {
                    buffer[bytes_read] = '\0';
                    char* com_saveptr = nullptr;
                    char* command = strtok_r(buffer, " ", &com_saveptr);
                    if (command == NULL) {
                        const char* response = "ERROR: Unable to parse command";
                        send(client_fd, response, strlen(response), 0);
                        closeConnection(client_fd);
                        continue;
                    }
                    char* key = strtok_r(nullptr, " ", &com_saveptr);
                    if (key) {
                        auto hash = hashFunc(key);
                        auto shardId = hash % workerThreadCount;
                        auto kSize = strlen(key) + 1;
                        auto& shard = serverShards[shardId];
                        std::lock_guard<std::mutex> lock(shardMutex);

                        if (strcmp(command, "GET") == 0) {
                            Query query{1, nullptr, hash, client_fd};

                            query.key = new char[kSize];
                            memcpy(query.key, key, kSize);
                            query.key[kSize-1] = '\0';

                            shard.queryQueue.push(query);
                            shard.in_semaphore.release();
                            shard.out_semaphore.acquire();
                        } else if (strcmp(command, "SET") == 0) {
                            char* value = strtok_r(nullptr, " ", &com_saveptr);
                            if (value) {
                                Command cmd{1, nullptr, nullptr, hash, client_fd};

                                auto vSize = strlen(value) + 1;
                                cmd.value = new char[vSize];
                                memcpy(cmd.value, value, vSize);
                                cmd.value[vSize-1] = '\0';

                                cmd.key = new char[kSize];
                                memcpy(cmd.key, key, kSize);
                                cmd.key[kSize-1] = '\0';

                                shard.commandQueue.push(cmd);
                                shard.in_semaphore.release();
                                shard.out_semaphore.acquire();
                            } else {
                                const char* response = "ERROR: Invalid SET command format";
                                #ifndef NDEBUG
                                std::cerr << response << " command:" << command << std::endl;
                                #endif
                                send(client_fd, response, strlen(response), 0);
                                closeConnection(client_fd);
                            }
                        }
                    } else { // TODO: support more commands here
                        const char* response = "ERROR: Unknown command";
                        #ifndef NDEBUG
                        std::cerr << response << ":" << command << std::endl;
                        #endif
                        numErrors++;
                        send(client_fd, response, strlen(response), 0);
                        closeConnection(client_fd);
                    }
                } else {
                    closeConnection(client_fd);
                }
            }
        }
    }
}

void CacheServer::closeConnection(int client_fd)
{
    shutdown(client_fd, SHUT_RDWR);
    close(client_fd);
    if(activeConnections > 0) { // TODO investigate
        --activeConnections;
    }
}

void CacheServer::metricsUpdater(std::queue<CacheServerMetrics>& channel, std::stop_token stopToken)
{
    while (!stopToken.stop_requested()) {
        {
            std::lock_guard<std::mutex> lock(metricsMutex);
            channel.push(CacheServerMetrics{ numErrors, activeConnections, numRequests });
        }
        std::this_thread::sleep_for(std::chrono::seconds(METRICS_UPDATE_FREQUENCY_SEC));
    }
}

std::pair<uint_fast16_t, uint_fast16_t> CacheServer::splitWorkerThreads(uint_fast16_t workerThreads)
{
    uint_fast16_t part1 = 1;
    auto oneFourth = workerThreads / 4;
    if (oneFourth > 1) {
        part1 = oneFourth;
    }

    uint_fast16_t part2 = workerThreads - part1;
    return {part1, part2};
}

void CacheServer::loadBalancer(int client_fd) {
    int epoll_fd = epollInstances[nextThread];    
    nextThread = (nextThread + 1) % epollThreadCount;

    epoll_event event{};
    event.events = EPOLLIN | EPOLLET;
    event.data.fd = client_fd;

    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &event) == -1) {
        perror("Failed to add client_fd to epoll");
        closeConnection(client_fd);
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
    setsockopt(server_fd, IPPROTO_TCP, TCP_NODELAY, (void*)&flag, sizeof(flag));
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag));
    int qlen = 5;
    setsockopt(server_fd, SOL_TCP, TCP_FASTOPEN, &qlen, sizeof(qlen));
    fcntl(server_fd, F_SETFL, O_NONBLOCK);

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

    auto totalThreadCount = std::thread::hardware_concurrency();

    auto workers = splitWorkerThreads(totalThreadCount);    
    epollThreadCount = workers.first;
    workerThreadCount = workers.second;

    std::cout << "Initializing " << epollThreadCount << " epoll instances" << std::endl;
    for (int i = 0; i < epollThreadCount; ++i) {
        int epoll_fd = epoll_create1(0);
        if (epoll_fd == -1) {
            perror("Failed to create epoll instance");
            return 1;
        }
        epollInstances.push_back(epoll_fd);    
        epollThreads.emplace_back(std::jthread([this, epoll_fd](std::stop_token stopToken)
        {
            epollLoop(epoll_fd, stopToken);
        }));
    }
    
    std::cout << "Initializing " << workerThreadCount << " server shards" << std::endl;
    serverShards = std::make_unique<ServerShard[]>(workerThreadCount);

    for (int i = 0; i < workerThreadCount; ++i) {
        serverShards[i].shardId = i;
        workerThreads.emplace_back(std::jthread([this, i](std::stop_token stopToken)
        {
            workerLoop(serverShards[i], stopToken);
        }));
    }
    
    metricsUpdaterThread = std::jthread([this, &channel](std::stop_token stopToken)
    {
        metricsUpdater(channel, stopToken);
    });

    std::cout << "Server started on port " << port << ", " << workerThreadCount << " worker threads are ready" << std::endl;

    while (!cancellationToken) {
        sockaddr_in client_address;
        socklen_t client_len = sizeof(client_address);
        int client_fd = accept(server_fd, (struct sockaddr*)&client_address, &client_len);

        if (client_fd >= 0) {
            ++activeConnections;
            ++numRequests;
            loadBalancer(client_fd);
        } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
            perror("Failed to accept connection");
        }     
    }

    std::cout << "Shutting down gracefully..." << std::endl;
    Stop();
    std::cout << "Server shut down successfully" << std::endl;
    return 0;    
}

void CacheServer::Stop()
{
    if(isRunning) {
        isRunning = false;

        std::cout << "Stopping epoll threads..." << std::endl;
        cancellationToken = true;

        for (auto& epoll_thread : epollThreads) {
            auto sSource = epoll_thread.get_stop_source();
            
            if (sSource.stop_possible()) {
                sSource.request_stop();
            }
        }
        std::cout << "Epoll threads stopped" << std::endl;

        std::cout << "Stopping worker threads..." << std::endl;
        cancellationToken = true;

        for (auto& worker_thread : workerThreads) {
            auto sSource = worker_thread.get_stop_source();
            
            if (sSource.stop_possible()) {
                sSource.request_stop();
            }
        }
        std::cout << "Worker threads stopped" << std::endl;

        std::cout << "Stopping metrics updater thread..." << std::endl;
        auto mu_sSource =  metricsUpdaterThread.get_stop_source();
        if (mu_sSource.stop_possible()) {
            mu_sSource.request_stop();
        }
        std::cout << "Metrics updater thread stopped" << std::endl;
    }
}
