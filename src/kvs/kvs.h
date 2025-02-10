#pragma once
#include <cstdint>
#include <cstring>
#include <string.h>
#include <charconv>
#include <iostream>
#include <cmath>
#include <queue>
#include <algorithm>
#include <future>
#include <atomic>
#include "../primegen/primegen.h"
#include "../hash/hash.h"
#include "../utils/trashcan.hpp"
#include "../non_copyable.h"

#ifndef NDEBUG
#include <chrono>
#endif

#define UNIT_SEPARATOR 0x1F
#define BUCKET_SIZE 4
#define MIN_COMPRESSED_SIZE 4
#define MAX_READ_WRITE_ATTEMPTS 5
#define RESIZE_THRESHOLD_PERCENTAGE 70
#define FREQ_DICT_SIZE 512
#define COMPRESSION_FREQUENCY 3


namespace kvs
{
    struct KeyValueStoreSettings {
        uint_fast64_t initialSize = 2053;
        bool compressionEnabled = true;
        bool usePrimeNumbers = true;
        bool enableTrashcan = true;
    };

    class KeyValueStore : NonCopyable {
        private:
            struct Entry {
                char* key;
                char* value;
                int_fast8_t occupied;
            };

            struct Bucket {
                Entry entries[BUCKET_SIZE];
            };

            Bucket *table;
            uint_fast64_t tableSize;
            uint_fast64_t numEntries;
            uint_fast64_t numCollisions;
            uint_fast32_t numResizes;


            int_fast8_t isResizing;
            void resize();
            void cleanTable(Bucket* tableToDelete, uint_fast64_t size);
            uint_fast64_t calcIndex(uint_fast64_t hash, int attempt, uint_fast64_t tableSize) const;
            
            bool usePrimeNumbers = true;
            Primegen primegen;

            bool enableTrashcan = true;
            std::unique_ptr<Trashcan<char>> trashcan;

            struct SubstringFrequency {
                const char* substring;
                size_t count;
            };

            bool compressionEnabled = true;
            std::unique_ptr<KeyValueStore> compressDictionary;
            void rebuildCompressionDictionary();
            char* compress(const char* value) const;
            char* decompress(const char* compressedValue) const;

        public:
            KeyValueStore(KeyValueStoreSettings settings = KeyValueStoreSettings{});
            ~KeyValueStore();

            uint_fast64_t getNumEntries() const noexcept {
                return numEntries;
            }

            bool set(const char *key, const char *value);
            bool set(const char *key, const char *value, uint_fast64_t hash); // use friend functions?

            const char *get(const char *key);
            const char *get(const char *key, uint_fast64_t hash); // use friend functions?
    };
}