#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>
#include <iostream>
#include <chrono>
#include <vector>
#include <atomic>
#include <fcntl.h>
#include <sys/epoll.h>
#include <prometheus/exposer.h>
#include <prometheus/registry.h>
#include <prometheus/counter.h>
#include <prometheus/histogram.h>
#include <prometheus/gauge.h>
#include "kvs.h"

#define MAX_EVENTS 10

using namespace prometheus;

// Structures for parsed commands and queries
struct Command {
    int commandCode; // 0: Reserved, 1: SET
    const char * key;   // String key
    const char * value; // String value
    int client_fd;  // Client file descriptor to send response
};

struct Query {
    int queryCode; // 0: Reserved, 1: GET
    const char * key;  // String key
    int client_fd; // Client file descriptor to send response
};

// Global variables
KeyValueStore keyValueStore;//(40000);
std::atomic<bool> running(true);

// Prometheus metrics
auto registry = std::make_shared<Registry>();

auto& request_count = BuildCounter()
                          .Name("endpoint_tcp_requests_total")
                          .Help("Total number of TCP requests")
                          .Register(*registry)
                          .Add({});

auto& error_count = BuildCounter()
                        .Name("endpoint_tcp_errors_total")
                        .Help("Total number of TCP errors")
                        .Register(*registry)
                        .Add({});

auto& request_latency = BuildHistogram()
                            .Name("endpoint_tcp_request_latency_seconds")
                            .Help("Histogram of request latencies in seconds")
                            .Register(*registry)
                            .Add({}, Histogram::BucketBoundaries{0.01, 0.1, 0.5, 1.0, 2.5, 5.0});

auto& storage_num_entries = BuildGauge()
                         .Name("storage_num_entries")
                         .Help("Number of entries in the storage")
                         .Register(*registry)
                         .Add({});

// Signal handler for graceful shutdown
void handleSignal(int signal) {
    if (signal == SIGINT) {
        running = false;
        std::cerr << "Shutting down gracefully..." << std::endl;
    }
}

void processCommand(const Command& command) {
    const char * response;

    if (command.commandCode == 1) { // SET command
        bool success = keyValueStore.set(command.key, command.value);
        if (success) {
            response = "OK";
        } else {
            response = "ERROR: Storage full";
        }
    } else {
        response = "ERROR: Invalid command code";
        error_count.Increment(); // Increment error count
    }

    send(command.client_fd, response, strlen(response), 0);
    close(command.client_fd); // Close connection after sending response
}

void processQuery(const Query& query) {
    const char * response;

    if (query.queryCode == 1) { // GET query
        const char *value = keyValueStore.get(query.key);
        response = value ? value : "(nil)";
    } else {
        response = "ERROR: Invalid query code";
        error_count.Increment(); // Increment error count
    }

    send(query.client_fd, response, strlen(response), 0);
    close(query.client_fd); // Close connection after sending response
}

int main() {
    signal(SIGINT, handleSignal);

    const int PORT = 9001;

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        std::cerr << "Socket creation failed" << std::endl;
        return 1;
    }

    fcntl(server_fd, F_SETFL, O_NONBLOCK);

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        std::cerr << "Bind failed" << std::endl;
        close(server_fd);
        return 1;
    }

    if (listen(server_fd, 3) < 0) {
        std::cerr << "Listen failed" << std::endl;
        close(server_fd);
        return 1;
    }

    std::cout << "Server started on port " << PORT << std::endl;

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

    Exposer exposer{"0.0.0.0:8080"}; // Expose on port 8080
    exposer.RegisterCollectable(registry);
    std::cout << "Metrics server started on port 8080" << std::endl;

    std::cout << "TCP server is ready to process incoming connections" << std::endl;
    while (running) {
        int event_count = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
        for (int i = 0; i < event_count; ++i) {
            if (events[i].data.fd == server_fd) {
                sockaddr_in client_address;
                socklen_t client_len = sizeof(client_address);
                int client_fd = accept(server_fd, (struct sockaddr *)&client_address, &client_len);

                if (client_fd >= 0) {
                    // fcntl(client_fd, F_SETFL, O_NONBLOCK);

                    char buffer[1024] = {0};
                    int bytes_read = read(client_fd, buffer, sizeof(buffer) - 1);
                    if (bytes_read > 0) {
                        buffer[bytes_read] = '\0';
                        char *command = strtok(buffer, " ");

                        if (strcmp(command, "SET") == 0) {
                            char *key = strtok(nullptr, " ");
                            char *value = strtok(nullptr, " ");
                            if (key && value) {
                                processCommand(Command{1, key, value, client_fd});
                            } else {
                                const char * response = "ERROR: Invalid SET command format";
                                send(client_fd, response, strlen(response), 0);
                                close(client_fd);
                            }
                        } else if (strcmp(command, "GET") == 0) {
                            char *key = strtok(nullptr, " ");
                            if (key) {
                                processQuery(Query{1, key, client_fd});
                            } else {
                                const char * response = "ERROR: Invalid GET command format";
                                send(client_fd, response, strlen(response), 0);
                                close(client_fd);
                            }
                        } else {
                            const char * response = "ERROR: Unknown command";
                            send(client_fd, response, strlen(response), 0);
                            close(client_fd);
                        }
                    } else {
                        close(client_fd);
                    }
                }
                storage_num_entries.Set(keyValueStore.getNumEntries());   
            }
        }
    }

    close(server_fd);
    close(epoll_fd);

    std::cout << "Server shut down successfully" << std::endl;
    return 0;
}
