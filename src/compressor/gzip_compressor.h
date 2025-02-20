#pragma once
#include <stdexcept>
#include <cstring>
#include <zlib.h>

#ifndef NDEBUG
#include <iostream>
#endif

#define CHUNK_SIZE 16384  // Buffer size for zlib operations
#define INVALID_INPUT -999
#define OPERATION_SUCCESS 0


/// @brief Result of compress operation
struct CompressResult {
    /// @brief Pointer to compressed string data location
    char* data;
    /// @brief Size of data
    size_t size;
    /// @brief Return code of underlying zlib execution, 0 - on success, -999 on invalid input, non zero on error. Check complete list of result codes here: https://www.zlib.net/manual.html
    int operationResult;
};

/// @brief Result of decompress operation
struct DecompressResult {
    /// @brief Pointer to decompressed string data location
    char* data;
    /// @brief Return code of underlying zlib execution, 0 - on success, -999 on invalid input, non zero on error. Check complete list of result codes here: https://www.zlib.net/manual.html
    int operationResult;
};

class GzipCompressor {
    public:
        /// @brief Performs gzip compression for the input string, you are responsible to delete[] the memory or capture it with smart pointer!
        /// @param input Input string
        /// @return Pointer to compressed string on success, nullptr on error
        static CompressResult Compress(const char* input);

        /// @brief Performs gzip decompression for the input string, you are responsible to delete[] the memory or capture it with smart pointer! 
        /// @param input Compressed string
        /// @return Pointer to decompressed string on success, nullptr on error
        static DecompressResult Decompress(const char* input, size_t input_size);
};
