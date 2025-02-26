#include <gtest/gtest.h>
#include <cstring>
#include <iostream>
#include <cstdlib>
#include <unistd.h>
#include <filesystem>
#include <fstream>
#include "kvs.h"
#include "../env.hpp"

using namespace kvs;
namespace fs = std::filesystem;

// Number of elements to test
const size_t NUM_ELEMENTS = getFromEnv<int>("NUM_ELEMENTS", true);

// Helper function to generate keys
auto generateKey(int_fast64_t index) {
    size_t numDigits = snprintf(nullptr, 0, "%zu", index);
    size_t keySize = 3 + numDigits + 1; // "key" + index + '\0'
    auto key = new char[keySize];
    snprintf(key, keySize, "key%zu", index);
    return key;
}

// Helper function to generate values
auto generateValue(int_fast64_t index) {
    size_t numDigits = snprintf(nullptr, 0, "%zu", index);
    size_t valueSize = 5 + numDigits + 1; // "value" + index + '\0'
    auto value = new char[valueSize];
    snprintf(value, valueSize, "value%zu", index);
    return value;
}

// Test storing and retrieving large JSON files
TEST(KeyValueStoreTest, LargeJSONFiles) {
    KeyValueStore kvStore;
    std::string dataPath = "../tests/data";

    for (const auto& entry : fs::directory_iterator(dataPath)) {
        if (entry.path().extension() == ".json") {
            std::ifstream file(entry.path());
            ASSERT_TRUE(file.is_open());

            std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            std::string key = entry.path().stem().string();
            
            ASSERT_TRUE(kvStore.set(key.c_str(), content.c_str()));
        }
    }
    
    for (const auto& entry : fs::directory_iterator(dataPath)) {
        if (entry.path().extension() == ".json") {
            std::ifstream file(entry.path());
            ASSERT_TRUE(file.is_open());
            
            std::string originalContent((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            std::string key = entry.path().stem().string();
            
            auto kvsValue = kvStore.get(key.c_str());
            ASSERT_NE(kvsValue, nullptr);
            ASSERT_STREQ(kvsValue, originalContent.c_str());
        }
    }
}

// Test adding and retrieving elements
TEST(KeyValueStoreTest, AddAndRetrieveElements) {
    KeyValueStore kvStore;
    for (int_fast64_t i = 0; i < NUM_ELEMENTS; ++i) {
        auto key = generateKey(i);
        auto value = generateValue(i);
        ASSERT_TRUE(kvStore.set(key, value));
        delete[] key;
        delete[] value;
    }
    usleep(1000000);
    for (int_fast64_t i = 0; i < NUM_ELEMENTS; ++i) {
        auto key = generateKey(i);
        auto value = generateValue(i);
        auto kvsValue = kvStore.get(key);
        ASSERT_NE(kvsValue, nullptr);
        ASSERT_STREQ(kvsValue, value);
        delete[] key;
        delete[] value;
    }
}

// Test overwriting elements
TEST(KeyValueStoreTest, OverwriteElements) {
    KeyValueStore kvStore;
    for (int_fast64_t i = 0; i < NUM_ELEMENTS; ++i) {
        auto key = generateKey(i);
        auto value = generateValue(i);
        ASSERT_TRUE(kvStore.set(key, value));
        delete[] key;
        delete[] value;
    }
    for (int_fast64_t i = 0; i < NUM_ELEMENTS; ++i) {
        auto key = generateKey(i);
        size_t numDigits = snprintf(nullptr, 0, "%zu", i);
        size_t newValueSize = 9 + numDigits + 1; // "new_value" + index + '\0'
        auto newValue = new char[newValueSize];
        snprintf(newValue, newValueSize, "new_value%zu", i);
        ASSERT_TRUE(kvStore.set(key, newValue));
        delete[] key;
        delete[] newValue;
    }
    usleep(1000000);
    for (int_fast64_t i = 0; i < NUM_ELEMENTS; ++i) {
        auto key = generateKey(i);
        size_t numDigits = snprintf(nullptr, 0, "%zu", i);
        size_t expectedValueSize = 9 + numDigits + 1;
        auto expectedValue = new char[expectedValueSize];
        snprintf(expectedValue, expectedValueSize, "new_value%zu", i);
        auto kvsValue = kvStore.get(key);
        ASSERT_NE(kvsValue, nullptr);
        ASSERT_STREQ(kvsValue, expectedValue);
        delete[] key;
        delete[] expectedValue;
    }
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
