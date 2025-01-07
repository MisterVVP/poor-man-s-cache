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
#include "kvs.h"

#define MAX_EVENTS 10
#define THREAD_POOL_SIZE 4

#ifdef DEBUG
#include <chrono>
#endif

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
};

TaskQueue taskQueue;
std::atomic<bool> running(true);

void workerThread(KeyValueStore &keyValueStore, int &epoll_fd) {
    while (running) {
        int client_fd = taskQueue.pop();

        char buffer[1024] = {0};
        int bytes_read = read(client_fd, buffer, sizeof(buffer));

        if (bytes_read > 0) {
#ifdef DEBUG
            auto start = std::chrono::high_resolution_clock::now();
#endif
            buffer[bytes_read] = '\0'; // Ensure null-terminated string
            
            // Parse and process the command
            char cmd[10] = {0};
            char key[256] = {0};
            char value[256] = {0};

            const char *response;
            if (sscanf(buffer, "%9s %255s %255[^\n]", cmd, key, value) >= 2) {
                if (strcmp(cmd, "SET") == 0) {
                    response = keyValueStore.set(key, value) ? "OK" : "ERROR: Storage full";
                } else if (strcmp(cmd, "GET") == 0) {
                    response = keyValueStore.get(key);
                    response = response ? response : "(nil)";
                } else {
                    response = "ERROR: Unknown command";
                }
            } else {
                response = "ERROR: Invalid command";
            }

            send(client_fd, response, strlen(response), 0);
#ifdef DEBUG
            auto end = std::chrono::high_resolution_clock::now();
            std::chrono::duration<double> elapsed = end - start;
            std::cout << "Processed request in " << elapsed.count() * 1000 << " milliseconds" << std::endl;
#endif
        }

        // Ensure client socket cleanup
        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, nullptr);
        close(client_fd);
    }
}

int main() {
    const int PORT = 9001;

    // Create a TCP socket
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == 0) {
        std::cerr << "Socket creation failed";
        return 1;
    }

    // Set socket to non-blocking
    fcntl(server_fd, F_SETFL, O_NONBLOCK);

    // Configure server address structure
    sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    // Bind the socket to the port
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        std::cerr << "Bind failed";
        close(server_fd);
        return 1;
    }

    // Start listening for connections
    if (listen(server_fd, 3) < 0) {
        std::cerr << "Listen failed";
        close(server_fd);
        return 1;
    }

    std::cout << "Server started on port " << PORT << std::endl;

    // Initialize key-value store
    KeyValueStore keyValueStore;

    // Create epoll instance
    int epoll_fd = epoll_create1(0);
    if (epoll_fd == -1) {
        std::cerr << "Failed to create epoll instance" << std::endl;
        close(server_fd);
        return 1;
    }

    epoll_event event;
    event.events = EPOLLIN;
    event.data.fd = server_fd;

    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &event) == -1) {
        std::cerr << "Failed to add server socket to epoll" << std::endl;
        close(server_fd);
        close(epoll_fd);
        return 1;
    }

    epoll_event events[MAX_EVENTS];

    // Create worker threads
    std::vector<std::thread> threads;
    for (int i = 0; i < THREAD_POOL_SIZE; ++i) {
        threads.emplace_back(workerThread, std::ref(keyValueStore), std::ref(epoll_fd));
    }

    while (running) {
        int event_count = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);

        for (int i = 0; i < event_count; ++i) {
            if (events[i].data.fd == server_fd) {
                sockaddr_in client_address;
                socklen_t client_len = sizeof(client_address);
                int client_fd = accept(server_fd, (struct sockaddr *)&client_address, &client_len);

                if (client_fd >= 0) {
                    fcntl(client_fd, F_SETFL, O_NONBLOCK);

                    event.events = EPOLLIN | EPOLLET;
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

    return 0;
}
