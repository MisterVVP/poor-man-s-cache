#include "gzip_compressor.h"

CompressResult GzipCompressor::Compress(const char* input) {
    if (!input || *input == '\0') return { nullptr, 0, INVALID_INPUT };

    auto input_length = strlen(input);
    z_stream strm{};
    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;

    auto operationResult = deflateInit2(&strm, Z_BEST_COMPRESSION, Z_DEFLATED, 15 + 16, 8, Z_DEFAULT_STRATEGY);
    
    if (operationResult != Z_OK) {
        return { nullptr, 0, operationResult };
    }

    strm.avail_in = static_cast<uInt>(input_length);
    strm.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(input));

    size_t buffer_size = CHUNK_SIZE;
    
    auto output = new char[buffer_size];
    size_t total_size = 0;

    do {
        if (total_size + CHUNK_SIZE > buffer_size) {
            buffer_size *= 2;
            auto new_output = new char[buffer_size];
            memcpy(new_output, output, total_size);
            delete[] output;
            output = new_output;
        }

        strm.avail_out = CHUNK_SIZE;
        strm.next_out = reinterpret_cast<Bytef*>(output + total_size);

        operationResult = deflate(&strm, Z_FINISH);
        total_size += (CHUNK_SIZE - strm.avail_out);
    } while (strm.avail_out == 0);

    deflateEnd(&strm);

    if (operationResult != Z_STREAM_END) {
        delete[] output;
        return { nullptr, 0, operationResult };
    }

    return { output, total_size, OPERATION_SUCCESS };
}

DecompressResult GzipCompressor::Decompress(const char* input, size_t input_size) {
    if (!input || input_size == 0) return { nullptr, INVALID_INPUT };

    z_stream strm{};
    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;
    strm.avail_in = static_cast<uInt>(input_size);
    strm.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(input));

    auto operationResult = inflateInit2(&strm, 15 + 16);
    if (operationResult != Z_OK) {
        return { nullptr, operationResult };
    }

    size_t buffer_size = CHUNK_SIZE;
    auto output = new char[buffer_size];
    size_t total_size = 0;

    do {
        if (total_size + CHUNK_SIZE > buffer_size) {
            buffer_size *= 2;
            auto new_output = new char[buffer_size];
            memcpy(new_output, output, total_size);
            delete[] output;
            output = new_output;
        }

        strm.avail_out = CHUNK_SIZE;
        strm.next_out = reinterpret_cast<Bytef*>(output + total_size);

        operationResult = inflate(&strm, Z_NO_FLUSH);
        total_size += (CHUNK_SIZE - strm.avail_out);

        if (operationResult == Z_STREAM_ERROR) {
            inflateEnd(&strm);
            delete[] output;
            return { nullptr, operationResult };
        }
        if (operationResult == Z_DATA_ERROR) {
            inflateEnd(&strm);
            delete[] output;
            return { nullptr, operationResult };
        }
    } while (operationResult != Z_STREAM_END);

    inflateEnd(&strm);

    if (operationResult != Z_STREAM_END) {
        delete[] output;
        return { nullptr, operationResult };
    }

    char* decompressed = new char[total_size + 1];
    std::memcpy(decompressed, output, total_size);
    decompressed[total_size] = '\0';
    delete[] output;

    return { decompressed, OPERATION_SUCCESS };
}

