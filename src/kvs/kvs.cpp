#include "kvs.h"

using namespace kvs;

KeyValueStore::KeyValueStore(KeyValueStoreSettings settings)
    : tableSize(settings.initialSize),
      numEntries(0),
      numCollisions(0),
      numResizes(0),
      isResizing(false),
      compressionEnabled(settings.compressionEnabled),
      usePrimeNumbers(settings.usePrimeNumbers),
      entryPool(settings.initialSize) {
#ifndef NDEBUG
    std::cout << "Table initialization started! initialSize = " << tableSize << " usePrimeNumbers = " << usePrimeNumbers 
              << " compressionEnabled = " << compressionEnabled << std::endl;
#endif
    table = new Bucket[tableSize];
    initializeTable(table, tableSize);
#ifndef NDEBUG
    std::cout << "Table initialization finished!" << std::endl;
#endif
}

KeyValueStore::~KeyValueStore() {
#ifndef NDEBUG
    std::cout << "Destroying KeyValueStore... Entries: " << numEntries << ", Table Size: " << tableSize << std::endl;
#endif
    cleanTable(table, tableSize);
}


inline void KeyValueStore::initializeTable(Bucket *table, uint_fast64_t size) {
    for (uint_fast64_t i = 0; i < size; ++i) {
        for (int j = 0; j < BUCKET_SIZE; ++j) {
            table[i].entries[j] = 0;
        }
    }
}

inline void KeyValueStore::cleanTable(Bucket *tableToDelete, uint_fast64_t size) {
#ifndef NDEBUG
        std::cout << "Table cleanup started! size = " << size << std::endl;
#endif
        for (uint_fast64_t i = 0; i < size; ++i) {
            for (int j = 0; j < BUCKET_SIZE; ++j) {
                auto entryIdx = tableToDelete[i].entries[j];
                if (!entryIdx) continue;
                entryPool.deallocate(entryIdx);
            }
        }
        delete[] tableToDelete;
#ifndef NDEBUG
        std::cout << "Table cleanup finished!" << std::endl;
#endif
    }


inline uint_fast64_t KeyValueStore::calcIndex(uint_fast64_t hash, int attempt, uint_fast64_t tableSize) const {
    return (hash + attempt * attempt) % tableSize;
}

void KeyValueStore::resize() {
    isResizing = true;
#ifndef NDEBUG
    auto start = std::chrono::high_resolution_clock::now();
    std::cout << "Resizing started! numEntries = " << numEntries << " tableSize = " << tableSize << std::endl;
#endif
    uint_fast64_t newTableSize = usePrimeNumbers ? primegen.PopNext() : tableSize * 2;

    entryPool.expandPool(newTableSize);
    auto *newTable = new Bucket[newTableSize];
    initializeTable(newTable, newTableSize);

    #pragma omp parallel for schedule(dynamic)
    for (uint_fast64_t i = 0; i < tableSize; ++i) {
        for (int j = 0; j < BUCKET_SIZE; ++j) {
            auto entryIdx = table[i].entries[j];
            if (!entryIdx) {
                continue;
            }
            migrateEntry(newTable, newTableSize, entryIdx);
        }
    }

    delete[] table;
    table = newTable;
    tableSize = newTableSize;
    isResizing = false;
    ++numResizes;
#ifndef NDEBUG
    auto stop = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(stop - start);
    std::cout << "Resizing finished in " << duration.count() << " ms ! numEntries = " << numEntries 
              << " tableSize = " << tableSize << std::endl;
#endif
}

void KeyValueStore::migrateEntry(Bucket *newTable, uint_fast64_t newTableSize, uint_fast64_t entryIdx) {
    auto entry = entryPool.get(entryIdx);
    if (!entry.key) {
        return;
    }

    uint_fast64_t attempt = 0, idx;
    uint_fast64_t primaryHash = hashFunc(entry.key);
    bool migrated = false;

    do {
        idx = calcIndex(primaryHash, attempt++, newTableSize);
        for (int k = 0; k < BUCKET_SIZE; ++k) {
            if (!newTable[idx].entries[k]) {
                newTable[idx].entries[k] = entryIdx;
                migrated = true;
                break;
            }
        }
    } while (!migrated && attempt < MAX_READ_WRITE_ATTEMPTS);

    if (!migrated) {
        std::cerr << "Could not migrate entry, key = " << entry.key << ", entryIdx = " << entryIdx << std::endl;
    }
}

inline void KeyValueStore::copyEntry(Entry &dest, const Entry &src) {
    auto kSize = src.kSize;
    auto vSize = src.vSize;
    dest.key = new char[kSize];
    dest.value = new char[vSize];
    memcpy(dest.key, src.key, kSize);
    memcpy(dest.value, src.value, vSize);
    dest.kSize = src.kSize;
    dest.vSize = src.vSize;
    dest.compressed = src.compressed;
}

bool KeyValueStore::set(const char *key, const char *value) {
    auto kSize = strlen(key) + 1;
    auto vSize = strlen(value) + 1;
    auto primaryHash = hashFunc(key, kSize - 1);
    return set(key, kSize, value, vSize, primaryHash);
}

bool KeyValueStore::set(const char *key, const char *value, uint_fast64_t hash) {
    auto kSize = strlen(key) + 1;
    auto vSize = strlen(value) + 1;
    return set(key, kSize, value, vSize, hash);
}

bool KeyValueStore::set(const char *key, size_t kSize,
                        const char *value, size_t vSize,
                        uint_fast64_t hash) {
    if (numEntries >= ((tableSize * RESIZE_THRESHOLD_PERCENTAGE) / 100) && !isResizing) {
        resize();
    }

    uint_fast64_t attempt = 0, idx;

    do {
        idx = calcIndex(hash, attempt++, tableSize);
        for (int i = 0; i < BUCKET_SIZE; ++i) {
            auto entryIdx = table[idx].entries[i];
            if (entryIdx) {
                auto entry = entryPool.get(entryIdx);
                if (entry.key) {
                    if (entry.kSize == kSize && memcmp(entry.key, key, kSize) == 0) {
                        entryPool.deallocate(entryIdx);
                    } else {
                        continue;
                    }
                }
            }
            table[idx].entries[i] = insertEntry(key, value, kSize, vSize);
            return true;
        }
        numCollisions++;
    } while (attempt < MAX_READ_WRITE_ATTEMPTS);

#ifndef NDEBUG
    std::cerr << "Failed to insert key = " << key << " after " << attempt << " attempts." << std::endl;
#endif
    return false;
}

uint_fast64_t KeyValueStore::insertEntry(const char *key, const char *value, size_t kSize, size_t vSize) {
    auto poolEntry = entryPool.allocate();
    auto& allocatedEntry = poolEntry.entry;
    allocatedEntry.key = new char[kSize];
    memcpy(allocatedEntry.key, key, kSize);
    allocatedEntry.kSize = kSize;

    if (compressionEnabled && vSize >= MIN_SIZE_TO_COMPRESS) {
        auto compressed = GzipCompressor::Compress(value);
        if (compressed.operationResult == 0) {
            allocatedEntry.value = compressed.data;
            allocatedEntry.vSize = compressed.size;
            allocatedEntry.compressed = true;
        } else {
            allocatedEntry.value = new char[vSize];
            allocatedEntry.vSize = vSize;
            memcpy(allocatedEntry.value, value, vSize);
        }
    } else {
        allocatedEntry.value = new char[vSize];
        allocatedEntry.vSize = vSize;
        memcpy(allocatedEntry.value, value, vSize);
    }

    ++numEntries;
    return poolEntry.i;
}

const char* KeyValueStore::get(const char *key) {
    auto kSize = strlen(key) + 1;
    auto primaryHash = hashFunc(key, kSize - 1);
    return get(key, kSize, primaryHash);
}

const char* KeyValueStore::get(const char *key, uint_fast64_t hash) {
    auto kSize = strlen(key) + 1;
    return get(key, kSize, hash);
}

const char* KeyValueStore::get(const char *key, size_t kSize, uint_fast64_t hash) {
    uint_fast64_t attempt = 0, idx;
    do {
        idx = calcIndex(hash, attempt++, tableSize);
        for (int i = 0; i < BUCKET_SIZE; ++i) {
            auto entryIdx = table[idx].entries[i];
            if (!entryIdx) {
                continue;
            }

            auto entry = entryPool.get(entryIdx);
            if (!entry.key) {
                continue;
            }

            if (entry.kSize == kSize && memcmp(entry.key, key, kSize) == 0) {
                return entry.compressed ? decompressEntry(entry) : entry.value;
            }
        }
    } while (attempt < MAX_READ_WRITE_ATTEMPTS);

    return nullptr;
}

inline const char* KeyValueStore::decompressEntry(const Entry &entry) {
    auto decompressed = GzipCompressor::Decompress(entry.value, entry.vSize);
    return decompressed.operationResult == 0 ? decompressed.data : nullptr;
}

bool kvs::KeyValueStore::del(const char *key)
{
    auto kSize = strlen(key) + 1;
    auto primaryHash = hashFunc(key, kSize - 1);
    return del(key, kSize, primaryHash);
}

bool kvs::KeyValueStore::del(const char *key, uint_fast64_t hash)
{
    auto kSize = strlen(key) + 1;
    return del(key, kSize, hash);
}

bool kvs::KeyValueStore::del(const char *key, size_t kSize, uint_fast64_t hash)
{
    // TODO: consider shrinking in future
    uint_fast64_t attempt = 0, idx;

    do {
        idx = calcIndex(hash, attempt++, tableSize);
        for (int i = 0; i < BUCKET_SIZE; ++i) {
            auto entryIdx = table[idx].entries[i];
            if (!entryIdx) {
                continue;
            }

            auto entry = entryPool.get(entryIdx);
            if (!entry.key) {
                continue;
            }

            if (entry.kSize == kSize && memcmp(entry.key, key, kSize) == 0) {
                entryPool.deallocate(entryIdx);
                return true;
            }
        }
    } while (attempt < MAX_READ_WRITE_ATTEMPTS);

#ifndef NDEBUG
    std::cerr << "Failed to find key during deletion, key = " << key << " after " << attempt << " attempts." << std::endl;
#endif
    return false;
}
