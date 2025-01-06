#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>
#include <iostream>
#include <chrono>
#include "kvs.h"

int main() {
    const int PORT = 9001;
    const int BUFFER_SIZE = 1024;
    char buffer[BUFFER_SIZE]; // Fixed-size buffer for handling incoming requests

    // Create a TCP socket
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == 0) {
        std::cerr << "Socket creation failed";
        return 1;
    }

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

    KeyValueStore keyValueStore;

    auto processCommand = [&keyValueStore](const char *command, size_t length) {
        // Parse the command using fixed-size buffers
        char cmd[10] = {0};
        char key[256] = {0};
        char value[256] = {0};

        if (sscanf(command, "%9s %255s %255[^\"]", cmd, key, value) >= 2) {
            if (strcmp(cmd, "SET") == 0) {
                if (keyValueStore.set(key, value)) {
                    return "OK";
                } else {
                    return "ERROR: Storage full";
                }
            } else if (strcmp(cmd, "GET") == 0) {
                const char *result = keyValueStore.get(key);
                return result ? result : "(nil)";
            } else {
                return "ERROR: Unknown command";
            }
        }

        return "ERROR: Invalid command";
    };

    while (true) {
        sockaddr_in client_address;
        socklen_t client_len = sizeof(client_address);
        int client_fd = accept(server_fd, (struct sockaddr *)&client_address, &client_len);
        if (client_fd < 0) {
            std::cerr << "Failed to accept connection" << std::endl;
            continue;
        }

        int bytes_read = read(client_fd, buffer, BUFFER_SIZE);
        if (bytes_read > 0) {
            //TODO: record performance metrics here and write to stdout only on graceful shutdown
            //auto start = std::chrono::high_resolution_clock::now();

            buffer[bytes_read] = '\0'; // Ensure null-terminated string
            const char *response = processCommand(buffer, bytes_read);

            send(client_fd, response, strlen(response), 0);

            //auto end = std::chrono::high_resolution_clock::now();
            //std::chrono::duration<double> elapsed = end - start;
            //std::cout << "Processed request in " << elapsed.count() * 1000 << " milliseconds" << std::endl;
        } else {
            std::cout << "Received empty request" << std::endl;
        }

        close(client_fd);
    }

    close(server_fd);
    return 0;
}
