#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include <memory>
#include <cstdio>
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
#include "../non_copyable.h"
#include "../compressor/gzip_compressor.h"

#ifndef NDEBUG
#include <chrono>
#endif

#define UNIT_SEPARATOR 0x1F
#define BUCKET_SIZE 4
#define MIN_SIZE_TO_COMPRESS 30
#define MAX_READ_WRITE_ATTEMPTS 5
#define RESIZE_THRESHOLD_PERCENTAGE 70

namespace kvs
{
    struct KeyValueStoreSettings {
        uint_fast64_t initialSize = 2053;
        bool compressionEnabled = true;
        bool usePrimeNumbers = true;
    };

    struct Entry {
        char *key = nullptr;
        char *value = nullptr;
        size_t vSize = 0;
        bool occupied = false;
        bool compressed = false;
    };

    struct Bucket {
        uint_fast64_t entries[BUCKET_SIZE];
    };

    struct PoolEntry {
        uint_fast64_t i;
        Entry& entry;
    };

    class MemoryPool : NonCopyableOrMovable  { // TODO: try out plain C arrays here instead of std::vector
        private:
            std::vector<Entry> pool;
            std::atomic<size_t> freeIdx = 1;
            std::atomic<size_t> deallocations;
    
            void defragment() {
                size_t newFreeIdx = 1;
                for (size_t i = 0; i < freeIdx; ++i) {
                    if (pool[i].occupied) {
                        if (i != newFreeIdx) {
                            pool[newFreeIdx] = std::move(pool[i]);
                        }
                        ++newFreeIdx;
                    }
                }
                freeIdx = newFreeIdx;
                deallocations = 0;
            }
    
        public:
            explicit MemoryPool(size_t initialSize) : pool(initialSize), freeIdx(1), deallocations(0) {}

            inline void expandPool(size_t newSize) {
                pool.resize(newSize);
            }

            Entry& get(size_t i) {
                return pool[i];
            }

            PoolEntry allocate() {
                if (freeIdx == pool.capacity() - 1) {
                    expandPool(pool.capacity() * 1.5);
                }
                /* TODO: debug this
                if (deallocations >= pool.capacity() / 5) {
                    defragment();
                } */
                size_t i = freeIdx;
                ++freeIdx;
                return PoolEntry { i, pool[i] };
            }
    
            void deallocate(size_t i) {
                auto &entry = pool[i];
                entry.occupied = false;
                entry.compressed = false;
                entry.vSize = 0;
                delete[] entry.key;
                delete[] entry.value;
                entry.key = nullptr;
                entry.value = nullptr;
                ++deallocations;
            }
    };
    

    class KeyValueStore : NonCopyableOrMovable {
        private:
            Bucket *table;
            uint_fast64_t tableSize;
            uint_fast64_t numEntries;
            uint_fast64_t numCollisions;
            uint_fast32_t numResizes;

            MemoryPool entryPool;
            bool isResizing = false;
            void resize();
            void copyEntry(Entry &dest, const Entry &src);
            uint_fast64_t insertEntry(const char *key, const char *value, size_t kSize, size_t vSize);
            void migrateEntry(Bucket *newTable, uint_fast64_t newTableSize, uint_fast64_t entryIdx);
            const char* decompressEntry(const Entry &entry);
            void initializeTable(Bucket *table, uint_fast64_t size);
            void cleanTable(Bucket* tableToDelete, uint_fast64_t size);
            uint_fast64_t calcIndex(uint_fast64_t hash, int attempt, uint_fast64_t tableSize) const;
            
            bool usePrimeNumbers = true;
            Primegen primegen;

            struct SubstringFrequency {
                const char* substring;
                size_t count;
            };

            bool compressionEnabled = true;

        public:
            KeyValueStore(KeyValueStoreSettings settings = KeyValueStoreSettings{});
            ~KeyValueStore();

            uint_fast64_t getNumEntries() const noexcept {
                return numEntries;
            }

            bool set(const char *key, const char *value);
            bool set(const char *key, const char *value, uint_fast64_t hash); // use friend functions?

            const char* get(const char *key);
            const char* get(const char *key, uint_fast64_t hash); // use friend functions?
    };
}