#include "server.h"

int_fast8_t CacheServer::processCommand(const Command &command)
{
    const char* response = nullptr;
    int_fast8_t result = 0;
    if (command.commandCode == 1) {
        auto setRes = keyValueStore_ptr.get()->set(command.key, command.value);
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
    close(command.client_fd);

    return result;
}

int_fast8_t CacheServer::processQuery(const Query &query)
{
    const char* response = nullptr;
    int_fast8_t result = 0;
    if (query.queryCode == 1) {
        const char* value = keyValueStore_ptr.get()->get(query.key);
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
    close(query.client_fd);

    return result;
}

CacheServer::CacheServer(int port, std::shared_ptr<KeyValueStore> kvs_ptr, std::atomic<bool>& cToken): 
    keyValueStore_ptr(kvs_ptr), port(port), cancellationToken(cToken), numErrors(0)
{
}

int CacheServer::Start(std::queue<CacheServerMetrics>& channel)
{
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        std::cerr << "Socket creation failed" << std::endl;
        return 1;
    }

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

    if (listen(server_fd, 3) < 0) {
        std::cerr << "Listen failed" << std::endl;
        close(server_fd);
        return 1;
    }

    std::cout << "Server started on port " << port << std::endl;

    int epoll_fd = epoll_create1(0);
    if (epoll_fd == -1) {
        std::cerr << "Failed to create epoll instance" << std::endl;
        close(server_fd);
        return 1;
    }

    epoll_event event{};
    event.events = EPOLLIN;
    event.data.fd = server_fd;

    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &event) == -1) {
        std::cerr << "Failed to add server socket to epoll" << std::endl;
        close(server_fd);
        close(epoll_fd);
        return 1;
    }

    epoll_event events[MAX_EVENTS];

    std::cout << "TCP server is ready to process incoming connections" << std::endl;
    while (!cancellationToken) {
        channel.push(CacheServerMetrics{ numErrors, keyValueStore_ptr.get()->getNumEntries(), keyValueStore_ptr.get()->getNumResizes() });
        int event_count = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);

        for (int i = 0; i < event_count; ++i) {
            if (events[i].data.fd == server_fd) {
                sockaddr_in client_address;
                socklen_t client_len = sizeof(client_address);
                int client_fd = accept(server_fd, (struct sockaddr*)&client_address, &client_len);

                if (client_fd >= 0) {
                    char buffer[READ_BUFFER_SIZE] = {0};
                    int bytes_read = read(client_fd, buffer, sizeof(buffer) - 1);
                    if (bytes_read > 0) {
                        buffer[bytes_read] = '\0';
                        char* com_saveptr = nullptr;
                        char* command = strtok_r(buffer, " ", &com_saveptr);
                        if (command == NULL) {
                            const char* response = "ERROR: Unable to parse command";
                            send(client_fd, response, strlen(response), 0);
                            close(client_fd);
                            continue;
                        }
                        if (strcmp(command, "SET") == 0) {
                            char* key = strtok_r(nullptr, " ", &com_saveptr);
                            char* value = strtok_r(nullptr, " ", &com_saveptr);
                            if (key && value) {
                                Command cmd{1, nullptr, nullptr, client_fd};

                                auto kSize = strlen(key) + 1;
                                cmd.key = new char[kSize];
                                memcpy(cmd.key, key, kSize);
                                cmd.key[kSize-1] = '\0';

                                auto vSize = strlen(value) + 1;
                                cmd.value = new char[vSize];
                                memcpy(cmd.value, value, vSize);
                                cmd.value[vSize-1] = '\0';

                                numErrors += processCommand(cmd);

                                delete[] cmd.key;
                                delete[] cmd.value;
                            } else {
                                const char* response = "ERROR: Invalid SET command format";
                                #ifndef NDEBUG
                                std::cerr << response << " command:" << command << std::endl;
                                #endif
                                send(client_fd, response, strlen(response), 0);
                                close(client_fd);
                            }
                        } else if (strcmp(command, "GET") == 0) {
                            char* key = strtok_r(nullptr, " ", &com_saveptr);
                            if (key) {
                                Query query{1, nullptr, client_fd};                                
                                auto kSize = strlen(key) + 1;
                                query.key = new char[kSize];
                                memcpy(query.key, key, kSize);
                                query.key[kSize-1] = '\0';
                                numErrors += processQuery(query);
                                delete[] query.key;
                            } else {
                                const char* response = "ERROR: Invalid GET command format";
                                #ifndef NDEBUG
                                std::cerr << response << " command:" << command << std::endl;
                                #endif
                                numErrors++;
                                send(client_fd, response, strlen(response), 0);
                                close(client_fd);
                            }
                        } else {
                            const char* response = "ERROR: Unknown command";
                            #ifndef NDEBUG
                            std::cerr << response << ":" << command << std::endl;
                            #endif
                            numErrors++;
                            send(client_fd, response, strlen(response), 0);
                            close(client_fd);
                        }
                    } else {
                        close(client_fd);
                        continue;
                    }
                }
            }
        }        
    }
    std::cout << "Shutting down gracefully..." << std::endl;

    close(server_fd);
    close(epoll_fd);

    std::cout << "Server shut down successfully" << std::endl;
    return 0;
}

void CacheServer::Stop()
{
    cancellationToken = true;
}
