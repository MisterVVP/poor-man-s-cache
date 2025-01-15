#include <gtest/gtest.h>
#include <cstring>
#include <iostream>
#include <cstdlib>
#include <unistd.h>
#include <cstdint>
#include "kvs.h"
#include "env.h"

// Number of elements to test
const size_t NUM_ELEMENTS = getIntFromEnv("NUM_ELEMENTS", true);

// Helper function to generate keys
char* generateKey(int_fast64_t index) {
    // Calculate the size needed for "key" + digits of index + null terminator
    size_t numDigits = snprintf(nullptr, 0, "%zu", index);
    size_t keySize = 3 + numDigits + 1; // "key" + index + '\0'

    char* key = new char[keySize];
    snprintf(key, keySize, "key%zu", index);
    return key;
}

// Helper function to generate values
char* generateValue(int_fast64_t index) {
    // Calculate the size needed for "value" + digits of index + null terminator
    size_t numDigits = snprintf(nullptr, 0, "%zu", index);
    size_t valueSize = 5 + numDigits + 1; // "value" + index + '\0'

    char* value = new char[valueSize];
    snprintf(value, valueSize, "value%zu", index);
    return value;
}

// Test adding and retrieving elements
TEST(KeyValueStoreTest, AddAndRetrieveElements) {
    KeyValueStore kvStore;

    // Add elements to the key-value store
    for (int_fast64_t i = 0; i < NUM_ELEMENTS; ++i) {
        char* key = generateKey(i);
        char* value = generateValue(i);
        auto res = kvStore.set(key, value);
        ASSERT_TRUE(res);

        // Free allocated memory for key and value
        delete[] key;
        delete[] value;
    }
    usleep(1000000);
    // Retrieve elements and check correctness
    for (int_fast64_t i = 0; i < NUM_ELEMENTS; ++i) {
        char* key = generateKey(i);
        char* value = generateValue(i);

        const char* retrievedValue = kvStore.get(key);
        ASSERT_NE(retrievedValue, nullptr);
        EXPECT_STREQ(retrievedValue, value);

        // Free allocated memory for key and value
        delete[] key;
        delete[] value;
    }
}

// Test overwriting elements
TEST(KeyValueStoreTest, OverwriteElements) {
    KeyValueStore kvStore;

    // Add elements to the key-value store
    for (int_fast64_t i = 0; i < NUM_ELEMENTS; ++i) {
        char* key = generateKey(i);
        char* value = generateValue(i);
        auto res = kvStore.set(key, value);
        ASSERT_TRUE(res);

        // Free allocated memory for key and value
        delete[] key;
        delete[] value;
    }
    
    // Overwrite elements with new values
    for (int_fast64_t i = 0; i < NUM_ELEMENTS; ++i) {
        char* key = generateKey(i);

        // Calculate new value for overwriting
        size_t numDigits = snprintf(nullptr, 0, "%zu", i);
        size_t newValueSize = 9 + numDigits + 1; // "new_value" + index + '\0'

        char* newValue = new char[newValueSize];
        snprintf(newValue, newValueSize, "new_value%zu", i);

        ASSERT_TRUE(kvStore.set(key, newValue));

        // Free allocated memory for key and newValue
        delete[] key;
        delete[] newValue;
    }

    usleep(1000000);
    // Retrieve elements and check correctness of overwritten values
    for (int_fast64_t i = 0; i < NUM_ELEMENTS; ++i) {
        char* key = generateKey(i);

        // Calculate the expected overwritten value
        size_t numDigits = snprintf(nullptr, 0, "%zu", i);
        size_t expectedValueSize = 9 + numDigits + 1; // "new_value" + index + '\0'

        char* expectedValue = new char[expectedValueSize];
        snprintf(expectedValue, expectedValueSize, "new_value%zu", i);

        const char* retrievedValue = kvStore.get(key);
        ASSERT_NE(retrievedValue, nullptr);
        EXPECT_STREQ(retrievedValue, expectedValue);

        // Free allocated memory for key and expectedValue
        delete[] key;
        delete[] expectedValue;
    }
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
