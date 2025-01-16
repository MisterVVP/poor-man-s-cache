#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>
#include <iostream>
#include <atomic>
#include <coroutine>
#include <chrono>
#include <vector>
#include <functional>
#include <fcntl.h>
#include <sys/epoll.h>
#include "env.h"
#include "metrics.h"
#include "kvs.h"

#define MAX_EVENTS 24

// Encapsulate globals in a struct
struct ServerContext {
    KeyValueStore keyValueStore;
    std::atomic<bool> running{true};
};

// Structures for parsed commands and queries
struct Command {
    int commandCode; // 0: Reserved, 1: SET
    const char* key; // String key
    const char* value; // String value
    int client_fd;    // Client file descriptor to send response
};

struct Query {
    int queryCode;  // 0: Reserved, 1: GET
    const char* key; // String key
    int client_fd;  // Client file descriptor to send response
};

task metricsUpdater(ServerContext& context) {
    for (;;) {
        storage_num_entries.Set(context.keyValueStore.getNumEntries());
        storage_num_resizes.Set(context.keyValueStore.getNumResizes());
        co_await switch_to_main();
    }
}

void processCommand(const Command& command, ServerContext& context) {
    const char* response;

    if (command.commandCode == 1) { // SET command
        bool success = context.keyValueStore.set(command.key, command.value);
        response = success ? "OK" : "ERROR: Storage full";
    } else {
        response = "ERROR: Invalid command code";
        error_count.Increment(); // Increment error count
    }

    send(command.client_fd, response, strlen(response), 0);
    close(command.client_fd); // Close connection after sending response
}

void processQuery(const Query& query, ServerContext& context) {
    const char* response;

    if (query.queryCode == 1) { // GET query
        const char* value = context.keyValueStore.get(query.key);
        response = value ? value : "(nil)";
    } else {
        response = "ERROR: Invalid query code";
        error_count.Increment(); // Increment error count
    }

    send(query.client_fd, response, strlen(response), 0);
    close(query.client_fd); // Close connection after sending response
}

int main() {
    ServerContext context;
    
    // Set up signal handling
    static std::function<void(int)> signalHandler = [&context](int signal) {
        if (signal == SIGINT) {
            context.running = false;
            std::cerr << "Shutting down gracefully..." << std::endl;
        }
    };

    auto signalDispatcher = [] (int signal) {
        if (signalHandler) {
            signalHandler(signal);
        }
    };

    signal(SIGINT, signalDispatcher); // Redirect signal to dispatcher

    auto server_port = getIntFromEnv("SERVER_PORT", true);
    auto metrics_port = getIntFromEnv("METRICS_PORT", true);

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        std::cerr << "Socket creation failed" << std::endl;
        return 1;
    }

    fcntl(server_fd, F_SETFL, O_NONBLOCK);

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(server_port);

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

    std::cout << "Server started on port " << server_port << std::endl;

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

    auto metrics_url = std::format("0.0.0.0:{}", metrics_port);
    Exposer exposer{metrics_url};
    exposer.RegisterCollectable(registry);

    std::cout << "Metrics server started on port " << metrics_port << std::endl;
    std::cout << "TCP server is ready to process incoming connections" << std::endl;

    while (context.running) {
        int event_count = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);

        for (int i = 0; i < event_count; ++i) {
            if (events[i].data.fd == server_fd) {
                sockaddr_in client_address;
                socklen_t client_len = sizeof(client_address);
                int client_fd = accept(server_fd, (struct sockaddr*)&client_address, &client_len);

                if (client_fd >= 0) {
                    char buffer[1024] = {0};
                    int bytes_read = read(client_fd, buffer, sizeof(buffer) - 1);
                    if (bytes_read > 0) {
                        buffer[bytes_read] = '\0';
                        char* command = strtok(buffer, " ");

                        if (strcmp(command, "SET") == 0) {
                            char* key = strtok(nullptr, " ");
                            char* value = strtok(nullptr, " ");
                            if (key && value) {
                                processCommand(Command{1, key, value, client_fd}, context);
                            } else {
                                const char* response = "ERROR: Invalid SET command format";
                                send(client_fd, response, strlen(response), 0);
                                close(client_fd);
                            }
                        } else if (strcmp(command, "GET") == 0) {
                            char* key = strtok(nullptr, " ");
                            if (key) {
                                processQuery(Query{1, key, client_fd}, context);
                            } else {
                                const char* response = "ERROR: Invalid GET command format";
                                send(client_fd, response, strlen(response), 0);
                                close(client_fd);
                            }
                        } else {
                            const char* response = "ERROR: Unknown command";
                            send(client_fd, response, strlen(response), 0);
                            close(client_fd);
                        }
                    } else {
                        close(client_fd);
                    }
                }
            }
        }
        metricsUpdater(context);
    }

    close(server_fd);
    close(epoll_fd);

    std::cout << "Server shut down successfully" << std::endl;
    return 0;
}
