#include "kvs.h"
#include <gtest/gtest.h>
#include <string>
#include <vector>
#include <iostream>

// Helper function to generate test data
std::vector<std::pair<std::string, std::string>> generateTestData(size_t count) {
    std::vector<std::pair<std::string, std::string>> data;
    for (size_t i = 0; i < count; ++i) {
        data.emplace_back("key" + std::to_string(i), "value" + std::to_string(i));
    }
    return data;
}

// Test adding and retrieving elements
TEST(KeyValueStoreTest, AddAndRetrieveElements) {
    const size_t N = 10000; // Number of elements to test
    KeyValueStore kvStore;

    auto testData = generateTestData(N);

    // Add elements to the key-value store
    for (const auto& [key, value] : testData) {
        auto res = kvStore.set(key.c_str(), value.c_str());
        ASSERT_TRUE(res);
    }


    // Retrieve elements and check correctness
    for (const auto& [key, value] : testData) {
        auto retrievedValue = kvStore.get(key.c_str());
        ASSERT_NE(retrievedValue, nullptr);
        EXPECT_STREQ(retrievedValue, value.c_str());
    }
}

// Test overwriting elements
TEST(KeyValueStoreTest, OverwriteElements) {
    const size_t N = 10000; // Number of elements to test
    KeyValueStore kvStore;

    auto testData = generateTestData(N);

    // Add elements to the key-value store
    for (const auto& [key, value] : testData) {
        auto res = kvStore.set(key.c_str(), value.c_str());
        ASSERT_TRUE(res);
    }

    // Overwrite elements with new values
    for (size_t i = 0; i < N; ++i) {
        std::string newValue = "new_value" + std::to_string(i);
        ASSERT_TRUE(kvStore.set(testData[i].first.c_str(), newValue.c_str()));
    }


    // Retrieve elements and check correctness of overwritten values
    for (size_t i = 0; i < N; ++i) {
        std::string newValue = "new_value" + std::to_string(i);
        const char* retrievedValue = kvStore.get(testData[i].first.c_str());
        ASSERT_NE(retrievedValue, nullptr);
        EXPECT_STREQ(retrievedValue, newValue.c_str());
    }
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
