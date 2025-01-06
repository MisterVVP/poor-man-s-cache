#include <iostream>
#include <cstdlib>
#include "App.h"
#include <unordered_map>
#include <string>
#include <iostream>

// In-memory key-value store (temporary, will later replace STL)
std::unordered_map<std::string, std::string> keyValueStore;

// Function to process commands
std::string processCommand(const std::string &command) {
    // Split the command into parts (basic parsing)
    size_t firstSpace = command.find(' ');
    std::string cmd = command.substr(0, firstSpace);

    if (cmd == "SET") {
        size_t secondSpace = command.find(' ', firstSpace + 1);
        if (secondSpace == std::string::npos) {
            return "ERROR: Invalid SET command\n";
        }

        std::string key = command.substr(firstSpace + 1, secondSpace - firstSpace - 1);
        std::string value = command.substr(secondSpace + 1);

        keyValueStore[key] = value;
        return "OK\n";
    } else if (cmd == "GET") {
        std::string key = command.substr(firstSpace + 1);
        if (keyValueStore.find(key) != keyValueStore.end()) {
            return keyValueStore[key] + "\n";
        } else {
            return "(nil)\n";
        }
    } else {
        return "ERROR: Unknown command\n";
    }
}

struct PerSocketData {}; // Empty struct for WebSocket data

int main() {
    // Start the uWebSockets app
    uWS::App().
        ws<PerSocketData>("/*", { // Replace <nullptr> with <void>
            .message = [](auto *ws, std::string_view message, uWS::OpCode opCode) {
                // Process the message as a command
                std::string response = processCommand(std::string(message));
                ws->send(response, opCode);
            }
        }).
        listen(9001, [](auto *token) {
            if (token) {
                std::cout << "Server started on port 9001\n";
            } else {
                std::cerr << "Failed to start server\n";
            }
        }).
        run();

    return 0;
}

