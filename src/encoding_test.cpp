#include <gtest/gtest.h>
#include <cstring>
#include <iostream>
#include <cstdlib>
#include <unistd.h>
#include <iostream>
#include <cassert>
#include "delta_encoding.h"
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

TEST(Encoding, TestSimpleCase) {
    const char* input = "value4284521748";
    auto encoded = DeltaEncoding::encode(input);

    const char* decoded = DeltaEncoding::decode(encoded);

    EXPECT_STREQ(decoded, input);
}


TEST(Encoding, TestMassEncoding) {
    // Add elements to the key-value store
    for (int_fast64_t i = 0; i < NUM_ELEMENTS; ++i) {
        char* value = generateValue(i);
        auto encoded = DeltaEncoding::encode(value);

        const char* decoded = DeltaEncoding::decode(encoded);

        ASSERT_NE(decoded, nullptr);
        EXPECT_STREQ(decoded, value);
        delete[] value;
    }
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);   
    return RUN_ALL_TESTS();
}
