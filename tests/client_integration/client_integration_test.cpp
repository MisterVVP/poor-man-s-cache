#include "client/cache_client.hpp"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <random>
#include <sstream>
#include <string>
#include <thread>

namespace {

[[noreturn]] void fail(const std::string& message) {
    std::cerr << "[client-integration] " << message << std::endl;
    std::exit(EXIT_FAILURE);
}

void expect(bool condition, const std::string& message) {
    if (!condition) {
        fail(message);
    }
}

std::string randomSuffix() {
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<std::uint64_t> dist;
    std::ostringstream oss;
    oss << std::hex << dist(gen);
    return oss.str();
}

void connectWithRetry(pmc::CacheClient& client, int maxAttempts = 20) {
    using namespace std::chrono_literals;
    for (int attempt = 1; attempt <= maxAttempts; ++attempt) {
        try {
            client.connect();
            return;
        } catch (const std::exception& ex) {
            if (attempt == maxAttempts) {
                std::ostringstream oss;
                oss << "Failed to connect to cache server after " << maxAttempts
                    << " attempts: " << ex.what();
                fail(oss.str());
            }
            std::this_thread::sleep_for(250ms);
        }
    }
}

} // namespace

int main() {
    const char* hostEnv = std::getenv("CACHE_HOST");
    const char* portEnv = std::getenv("CACHE_PORT");

    pmc::CacheClient::Options options;
    options.host = hostEnv ? hostEnv : "127.0.0.1";
    if (portEnv != nullptr) {
        options.port = static_cast<std::uint16_t>(std::stoi(portEnv));
    }

    pmc::CacheClient client(options);
    connectWithRetry(client);

    const std::string keyPrefix = std::string("cpp-client-it-") + randomSuffix();
    const std::string key1 = keyPrefix + "-k1";
    const std::string key2 = keyPrefix + "-k2";
    const std::string value1 = "value-1";
    const std::string value2 = "value-2";

    // Basic CRUD semantics.
    {
        auto getMissing = client.get(key1);
        expect(getMissing.notFound(), "Expected missing key to return NotFound");

        auto setResponse = client.set(key1, value1);
        expect(setResponse.ok(), "SET should return OK result");

        auto getResponse = client.get(key1);
        expect(getResponse.ok(), "GET after SET should succeed");
        expect(getResponse.value == value1, "GET should return the stored value");

        auto delResponse = client.del(key1);
        expect(delResponse.ok(), "DEL should return OK for existing key");

        auto getDeleted = client.get(key1);
        expect(getDeleted.notFound(), "GET after DEL should return NotFound");
    }

    // Verify pipelining helpers and response bookkeeping work correctly.
    {
        const auto setFooId = client.enqueueSet(key1, value1);
        const auto setBarId = client.enqueueSet(key2, value2);
        const auto getFooId = client.enqueueGet(key1);
        const auto getBarId = client.enqueueGet(key2);
        const auto delFooId = client.enqueueDelete(key1);

        expect(client.pendingRequestCount() == 5, "All commands should be pending before flush");
        client.flush();

        // Wait for responses in an order different from their submission to ensure
        // cached responses are surfaced correctly.
        auto getFoo = client.waitFor(getFooId);
        expect(getFoo.ok(), "GET response should be OK");
        expect(getFoo.value == value1, "GET response should contain latest value");

        auto setFoo = client.waitFor(setFooId);
        expect(setFoo.ok(), "Queued SET response should be cached and retrievable");

        auto setBar = client.waitFor(setBarId);
        expect(setBar.ok(), "Second SET response should be OK");

        auto getBar = client.waitFor(getBarId);
        expect(getBar.ok(), "GET for second key should succeed");
        expect(getBar.value == value2, "GET for second key should return stored value");

        auto delFoo = client.waitFor(delFooId);
        expect(delFoo.ok(), "DEL should return OK for existing key");

        auto finalGet = client.get(key1);
        expect(finalGet.notFound(), "Key should be missing after deletion");
    }

    std::cout << "Client integration test completed successfully" << std::endl;
    return EXIT_SUCCESS;
}
