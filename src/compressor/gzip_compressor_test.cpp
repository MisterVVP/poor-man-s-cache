#include <gtest/gtest.h>
#include <zlib.h>
#include "gzip_compressor.hpp"

// Test compression and decompression of a normal string
TEST(GzipCompressorTest, CompressDecompress) {
    auto input = "Hello, Gzip!";
    
    auto compressed = GzipCompressor::Compress(input);
    ASSERT_NE(compressed.data, nullptr) << "Compression failed.";
    ASSERT_NE(compressed.size, 0) << "Compression failed.";
    ASSERT_EQ(compressed.operationResult, OPERATION_SUCCESS) << "Compression failed.";

    auto decompressed = GzipCompressor::Decompress(compressed.data, compressed.size);
    ASSERT_NE(decompressed.data, nullptr) << "Decompression failed.";
    ASSERT_EQ(decompressed.operationResult, OPERATION_SUCCESS) << "Decompression failed.";

    EXPECT_STREQ(decompressed.data, input) << "Decompressed string does not match original.";

    delete[] compressed.data;
    delete[] decompressed.data;
}

// Test empty string handling
TEST(GzipCompressorTest, CompressEmptyString) {
    auto input = "";
    
    auto compressed = GzipCompressor::Compress(input);
    ASSERT_EQ(compressed.data, nullptr) << "Compression of empty string should return nullptr.";
    ASSERT_EQ(compressed.size, 0) << "Compression size of empty string should be 0.";
    ASSERT_EQ(compressed.operationResult, INVALID_INPUT) << "Compression of empty string should return INVALID_INPUT result.";

    auto decompressed = GzipCompressor::Decompress(compressed.data, compressed.size);
    ASSERT_EQ(decompressed.data, nullptr) << "Decompression of empty result should return nullptr.";
    ASSERT_EQ(decompressed.operationResult, INVALID_INPUT) << "Decompression of empty result should return INVALID_INPUT result.";
}

// Test null input handling
TEST(GzipCompressorTest, CompressNullInput) {
    auto compressed = GzipCompressor::Compress(nullptr);
    ASSERT_EQ(compressed.data, nullptr) << "Compression of nullptr should return nullptr.";
    ASSERT_EQ(compressed.size, 0) << "Compression size of nullptr should be 0";
    ASSERT_EQ(compressed.operationResult, INVALID_INPUT) << "Compression of nullptr should return INVALID_INPUT result.";

    auto decompressed = GzipCompressor::Decompress(compressed.data, compressed.size);
    ASSERT_EQ(decompressed.data, nullptr) << "Decompression of nullptr should return nullptr.";
    ASSERT_EQ(decompressed.operationResult, INVALID_INPUT) << "Decompression of nullptr should return INVALID_INPUT result.";
}

// Test long string compression and decompression
TEST(GzipCompressorTest, CompressDecompressLongString) {
    auto input = "This is a long test string. " 
                        "It should be compressed and decompressed properly. "
                        "We are testing to see if gzip can handle long input.";

    auto compressed = GzipCompressor::Compress(input);
    ASSERT_NE(compressed.data, nullptr) << "Compression failed.";
    ASSERT_NE(compressed.size, 0) << "Compression failed.";
    ASSERT_EQ(compressed.operationResult, OPERATION_SUCCESS) << "Compression failed.";
    auto original_size = strlen(input);
    EXPECT_LT(compressed.size, original_size) << "Compression did not reduce size.";

    auto decompressed = GzipCompressor::Decompress(compressed.data, compressed.size);
    ASSERT_NE(decompressed.data, nullptr) << "Decompression failed.";
    ASSERT_EQ(decompressed.operationResult, OPERATION_SUCCESS) << "Decompression failed.";

    EXPECT_STREQ(decompressed.data, input) << "Decompressed string does not match original.";

    delete[] compressed.data;
    delete[] decompressed.data;
}

// Test compression efficiency (compressed size should be smaller than original)
TEST(GzipCompressorTest, CompressionReducesSize) {
    auto input = "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA";

    auto compressed = GzipCompressor::Compress(input);
    ASSERT_NE(compressed.data, nullptr) << "Compression failed.";
    ASSERT_NE(compressed.size, 0) << "Compression failed.";
    ASSERT_EQ(compressed.operationResult, OPERATION_SUCCESS) << "Compression failed.";

    auto original_size = strlen(input);
    EXPECT_LT(compressed.size, original_size) << "Compression did not reduce size.";

    delete[] compressed.data;
}

// Test decompressing invalid compressed data
TEST(GzipCompressorTest, DecompressInvalidData) {
    auto invalid_data = "Not a gzip string";
    
    auto decompressed = GzipCompressor::Decompress(invalid_data, strlen(invalid_data));
    ASSERT_EQ(decompressed.data, nullptr) << "Decompression of invalid data should return nullptr.";
    ASSERT_LT(decompressed.operationResult, OPERATION_SUCCESS) << "Decompression of invalid data should return negative result.";
}

