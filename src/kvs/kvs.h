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
        bool compressed = false;
        size_t nextFree = 0;
    };

    struct Bucket {
        uint_fast64_t entries[BUCKET_SIZE];
    };

    struct PoolEntry {
        uint_fast64_t i;
        Entry& entry;
    };

    
    class MemoryPool : NonCopyableOrMovable {
        private:
            Entry *pool;
            size_t capacity;
            std::atomic<size_t> freeListHead;
            Primegen primegen;
        public:
            explicit MemoryPool(size_t initialSize) 
                : capacity(initialSize), freeListHead(0) {
                pool = new Entry[capacity];
                for (size_t i = 1; i < capacity - 1; ++i) {
                    pool[i].nextFree = i + 1;
                }
                pool[capacity - 1].nextFree = 0;
                freeListHead = 1;
            }
        
            ~MemoryPool() {
                delete[] pool;
            }
        
            PoolEntry allocate() {
                if (freeListHead == 0) {
                    auto newCapacity = primegen.PopNext();
                    expandPool(newCapacity);
                }
                size_t i = freeListHead;
                freeListHead = pool[i].nextFree;
                return PoolEntry { i, pool[i] };
            }

            void deallocate(size_t i) {
                auto &entry = pool[i];
                delete[] entry.key;
                delete[] entry.value;
                entry.key = nullptr;
                entry.value = nullptr;
                entry.vSize = 0;
                entry.compressed = false;
        
                entry.nextFree = freeListHead;
                freeListHead = i;
            }

            Entry& get(size_t i) {
                return pool[i];
            }

            void expandPool(size_t newSize) {
                Entry *newPool = new Entry[newSize];
                memcpy(newPool, pool, capacity * sizeof(Entry));
                for (size_t i = capacity; i < newSize - 1; ++i) {
                    newPool[i].nextFree = i + 1;
                }
                newPool[newSize - 1].nextFree = 0;
        
                delete[] pool;
                pool = newPool;
                freeListHead = capacity;  
                capacity = newSize;
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