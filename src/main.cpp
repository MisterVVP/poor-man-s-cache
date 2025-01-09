#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>
#include <iostream>
#include <chrono>
#include <thread>
#include <vector>
#include <atomic>
#include <mutex>
#include <queue>
#include <fcntl.h>
#include <sys/epoll.h>
#include <condition_variable>
#include <prometheus/exposer.h>
#include <prometheus/registry.h>
#include <prometheus/counter.h>
#include <prometheus/histogram.h>
#include <prometheus/gauge.h>
#include "kvs.h"

#define MAX_EVENTS 10
#define THREAD_POOL_SIZE 4

using namespace prometheus;

// Thread-safe task queue
class TaskQueue {
private:
    std::queue<int> tasks;
    std::mutex queueMutex;
    std::condition_variable condition;

public:
    void push(int client_fd) {
        {
            std::lock_guard<std::mutex> lock(queueMutex);
            tasks.push(client_fd);
        }
        condition.notify_one();
    }

    int pop() {
        std::unique_lock<std::mutex> lock(queueMutex);
        condition.wait(lock, [this]() { return !tasks.empty(); });
        int client_fd = tasks.front();
        tasks.pop();
        return client_fd;
    }

    size_t size() {
        std::lock_guard<std::mutex> lock(queueMutex);
        return tasks.size();
    }
};

// Global variables
TaskQueue taskQueue;
std::atomic<bool> running(true);

// Prometheus metrics
auto registry = std::make_shared<Registry>();

auto& request_count = BuildCounter()
                          .Name("tcp_requests_total")
                          .Help("Total number of TCP requests")
                          .Register(*registry)
                          .Add({});

auto& error_count = BuildCounter()
                        .Name("tcp_errors_total")
                        .Help("Total number of TCP errors")
                        .Register(*registry)
                        .Add({});

auto& request_latency = BuildHistogram()
                            .Name("tcp_request_latency_seconds")
                            .Help("Histogram of request latencies in seconds")
                            .Register(*registry)
                            .Add({}, Histogram::BucketBoundaries{0.01, 0.1, 0.5, 1.0, 2.5, 5.0});

auto& queue_length = BuildGauge()
                         .Name("task_queue_length")
                         .Help("Number of tasks in the queue")
                         .Register(*registry)
                         .Add({});

// Signal handler for graceful shutdown
void handleSignal(int signal) {
    if (signal == SIGINT) {
        running = false;
        std::cerr << "Shutting down gracefully..." << std::endl;
    }
}

// Worker thread function
void workerThread(KeyValueStore &keyValueStore, int &epoll_fd) {
    while (running) {
        int client_fd = taskQueue.pop();

        queue_length.Set(taskQueue.size()); // Monitor queue size

        char buffer[1024] = {0};
        auto start = std::chrono::steady_clock::now(); // Start latency timer

        while (true) {
            int bytes_read = read(client_fd, buffer, sizeof(buffer) - 1);
            if (bytes_read > 0) {
                buffer[bytes_read] = '\0';

                request_count.Increment(); // Increment request count

                char cmd[10] = {0};
                char key[256] = {0};
                char value[256] = {0};

                const char *response;
                if (sscanf(buffer, "%9s %255s %255[^\n]", cmd, key, value) >= 2) {
                    if (strcmp(cmd, "SET") == 0) {
                        bool success = keyValueStore.set(key, value);
                        response = success ? "OK" : "ERROR: Storage full";
                    } else if (strcmp(cmd, "GET") == 0) {
                        response = keyValueStore.get(key);
                        response = response ? response : "(nil)";
                    } else {
                        response = "ERROR: Unknown command";
                    }
                } else {
                    response = "ERROR: Invalid command";
                    error_count.Increment(); // Increment error count
                }

                if (send(client_fd, response, strlen(response), 0) == -1) {
                    std::cerr << "Error sending response: " << strerror(errno) << std::endl;
                    error_count.Increment(); // Increment error count
                }
            } else if (bytes_read == 0) {
                // Client closed connection
                break;
            } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
                std::cerr << "Error reading from client socket: " << strerror(errno) << std::endl;
                error_count.Increment(); // Increment error count
                break;
            } else {
                break; // No more data
            }
        }

        auto end = std::chrono::steady_clock::now(); // End latency timer
        std::chrono::duration<double> elapsed_seconds = end - start;
        request_latency.Observe(elapsed_seconds.count()); // Record latency

        // Cleanup client socket
        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, nullptr);
        close(client_fd);
    }
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

    KeyValueStore keyValueStore;

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
    std::vector<std::thread> threads;

    // Start worker threads
    for (int i = 0; i < THREAD_POOL_SIZE; ++i) {
        threads.emplace_back(workerThread, std::ref(keyValueStore), std::ref(epoll_fd));
    }

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
                    fcntl(client_fd, F_SETFL, O_NONBLOCK);

                    event.events = EPOLLIN | EPOLLONESHOT;
                    event.data.fd = client_fd;

                    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &event) == -1) {
                        std::cerr << "Failed to add client socket to epoll" << std::endl;
                        close(client_fd);
                    }
                }
            } else {
                taskQueue.push(events[i].data.fd);
            }
        }
    }

    for (auto &thread : threads) {
        thread.join();
    }

    close(server_fd);
    close(epoll_fd);

    std::cout << "Server shut down cleanly." << std::endl;
    return 0;
}
