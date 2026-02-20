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
#include "../primegen/primegen.hpp"
#include "../hash/hash.hpp"
#include "../non_copyable.hpp"
#include "../compressor/gzip_compressor.hpp"

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
    static constexpr size_t SHRINK_THRESHOLD_DIVISOR = 4;
    static constexpr size_t SHRINK_CAPACITY_DIVISOR = 2;
    static constexpr size_t SHRINK_CHECK_INTERVAL = 262144;

    struct KeyValueStoreSettings {
        uint_fast64_t initialSize = 2053;
        bool compressionEnabled = true;
        bool usePrimeNumbers = true;
    };

    struct alignas(64) Entry {
        char *key = nullptr;
        char *value = nullptr;
        size_t vSize = 0;
        bool compressed = false;
        size_t nextFree = 0;
    };

    struct alignas(64) Bucket {
        uint_fast64_t entries[BUCKET_SIZE];
    };

    struct alignas(64) PoolEntry {
        uint_fast64_t i;
        Entry& entry;
    };

    struct GetResult {
        const char* value = nullptr;
        std::unique_ptr<char[]> ownedValue;
    };
    
    class MemoryPool : NonCopyableOrMovable {
        private:
            Entry *pool;
            size_t capacity;
            std::atomic<size_t> freeListHead;
            size_t minCapacity;
            Primegen primegen;
            size_t highestActiveIndex;
            bool highestActiveIndexDirty;
            size_t deleteOpsSinceShrinkCheck;

            static constexpr size_t POOL_RESERVED_ENTRY_COUNT = 1;
            static constexpr size_t POOL_MIN_CAPACITY = 2053;

            static inline void releaseEntryBuffers(Entry &entry) {
                delete[] entry.key;
                delete[] entry.value;
                entry.key = nullptr;
                entry.value = nullptr;
                entry.vSize = 0;
                entry.compressed = false;
            }

            void rebuildFreeList() {
                freeListHead = 0;
                for (size_t i = capacity - 1; i >= 1; --i) {
                    if (!pool[i].key) {
                        pool[i].nextFree = freeListHead;
                        freeListHead = i;
                    }
                }
            }

            void refreshHighestActiveIndex() {

                if (!highestActiveIndexDirty) {
                    return;
                }

                size_t i = std::min(highestActiveIndex, capacity - 1);
                while (i >= 1) {
                    if (pool[i].key) {
                        highestActiveIndex = i;
                        highestActiveIndexDirty = false;
                        return;
                    }
                    --i;
                }

                highestActiveIndex = 0;
                highestActiveIndexDirty = false;
            }

            bool shrinkTo(size_t newCapacity) {
                if (newCapacity >= capacity || newCapacity < minCapacity) {
                    return false;
                }

                refreshHighestActiveIndex();

                if (highestActiveIndex >= newCapacity) {
                    return false;
                }

                for (size_t i = newCapacity; i < capacity; ++i) {
                    releaseEntryBuffers(pool[i]);
                }

                auto *newPool = new Entry[newCapacity];
                memcpy(newPool, pool, newCapacity * sizeof(Entry));
                delete[] pool;
                pool = newPool;
                capacity = newCapacity;
                rebuildFreeList();
                deleteOpsSinceShrinkCheck = 0;
                return true;
            }

        public:
            explicit MemoryPool(size_t initialSize)
                : capacity(initialSize),
                  freeListHead(0),
                  minCapacity(initialSize),
                  highestActiveIndex(0),
                  highestActiveIndexDirty(false),
                  deleteOpsSinceShrinkCheck(0) {
                pool = new Entry[capacity];
                for (size_t i = 1; i < capacity - 1; ++i) {
                    pool[i].nextFree = i + 1;
                }
                pool[capacity - 1].nextFree = 0;
                freeListHead = 1;
            }
        
            ~MemoryPool() {
                for (size_t i = 1; i < capacity; ++i) {
                    releaseEntryBuffers(pool[i]);
                }
                delete[] pool;
            }
        
            PoolEntry allocate() {
                if (freeListHead == 0) {
                    auto newCapacity = primegen.PopNext();
                    while (newCapacity <= capacity) {
                        newCapacity = primegen.PopNext();
                    }

                    expandPool(newCapacity);
                }
                size_t i = freeListHead;
                freeListHead = pool[i].nextFree;
                highestActiveIndex = std::max(highestActiveIndex, i);
                return PoolEntry { i, pool[i] };
            }

            void deallocate(size_t i) {
                auto &entry = pool[i];
                releaseEntryBuffers(entry);

                if (i == highestActiveIndex) {
                    highestActiveIndexDirty = true;
                }
                ++deleteOpsSinceShrinkCheck;

                entry.nextFree = freeListHead;
                freeListHead = i;
            }

            Entry& get(size_t i) {
                return pool[i];
            }

            size_t getCapacity() const noexcept {
                return capacity;
            }

            void maybeShrink(size_t activeEntries) {

                if (capacity <= minCapacity || activeEntries >= (capacity / SHRINK_THRESHOLD_DIVISOR)) {
                    return;
                }

                if (deleteOpsSinceShrinkCheck >= SHRINK_CHECK_INTERVAL) {
                    auto targetCapacity = std::max(minCapacity, capacity / SHRINK_CAPACITY_DIVISOR);
                    auto requiredCapacity = std::max(POOL_MIN_CAPACITY, activeEntries + POOL_RESERVED_ENTRY_COUNT);

                    if (targetCapacity < requiredCapacity) {
                        targetCapacity = requiredCapacity;
                    }

                    shrinkTo(targetCapacity);
                }
            }

            void expandPool(size_t newSize) {
                if (newSize <= capacity) {
                    return;
                }

                Entry *newPool = new Entry[newSize];
                memcpy(newPool, pool, capacity * sizeof(Entry));
                for (size_t i = capacity; i < newSize - 1; ++i) {
                    newPool[i].nextFree = i + 1;
                }

                auto previousFreeListHead = freeListHead.load();
                newPool[newSize - 1].nextFree = previousFreeListHead;
        
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
            uint_fast64_t minTableSize;
            uint_fast64_t deleteOpsSinceTableShrinkCheck;

            MemoryPool entryPool;
            bool isResizing = false;
            void resize();
            void maybeShrinkTable();
            void rehash(uint_fast64_t newTableSize);
            void copyEntry(Entry &dest, const Entry &src);
            uint_fast64_t insertEntry(const char *key, const char *value, size_t kSize, size_t vSize);
            void migrateEntry(Bucket *newTable, uint_fast64_t newTableSize, uint_fast64_t entryIdx);
            std::unique_ptr<char[]> decompressEntry(const Entry &entry);
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

            uint_fast64_t getTableSize() const noexcept {
                return tableSize;
            }

            size_t getPoolCapacity() const noexcept {
                return entryPool.getCapacity();
            }

            bool set(const char *key, const char *value);
            bool set(const char *key, const char *value, uint_fast64_t hash);

            GetResult get(const char *key);
            GetResult get(const char *key, uint_fast64_t hash);

            bool del(const char *key);
            bool del(const char *key, uint_fast64_t hash);
    };
}
