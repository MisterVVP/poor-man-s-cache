#include "kvs.h"

using namespace kvs;

KeyValueStore::KeyValueStore(KeyValueStoreSettings settings)
    : tableSize(settings.initialSize),
      numEntries(0),
      numCollisions(0),
      numResizes(0),
      isResizing(false),
      compressionEnabled(settings.compressionEnabled),
      usePrimeNumbers(settings.usePrimeNumbers) {
#ifndef NDEBUG
    std::cout << "Table initialization started! initialSize = " << tableSize << " usePrimeNumbers = " << usePrimeNumbers << " compressionEnabled = " << compressionEnabled << std::endl;
#endif
    table = new Bucket[tableSize];
    for (uint_fast64_t i = 0; i < tableSize; ++i) {
        for (int j = 0; j < BUCKET_SIZE; ++j) {
            table[i].entries[j].occupied = false;
            table[i].entries[j].compressed = false;
        }
    }

#ifndef NDEBUG
    std::cout << "Table initialization finished!" << std::endl;
#endif
}

KeyValueStore::~KeyValueStore() {
    uint_fast64_t emptyEntries = 0;
    uint_fast64_t emptyBuckets = 0;
    for (uint_fast64_t i = 0; i < tableSize; ++i) {
        for (int j = 0; j < BUCKET_SIZE; ++j) {
            if (!table[i].entries[j].occupied) {
                ++emptyEntries;
                if (j == 0) {
                    ++emptyBuckets;
                }
            }
        }
    }
#ifndef NDEBUG
    std::cout << "Key value storage is being destroyed! numCollisions = " << numCollisions << " emptyBuckets = "
              << emptyBuckets << " emptyEntries = " << emptyEntries << " tableSize = " << tableSize 
              << " numEntries = " << numEntries << " numResizes = " << numResizes << std::endl;
#endif
    cleanTable(table, tableSize);
}

void KeyValueStore::cleanTable(Bucket *tableToDelete, uint_fast64_t size) {
#ifndef NDEBUG
    std::cout << "Table cleanup started! size = " << size << std::endl;
#endif
    for (uint_fast64_t i = 0; i < size; ++i) {
        for (int j = 0; j < BUCKET_SIZE; ++j) {
            if (tableToDelete[i].entries[j].occupied) {
                delete[] tableToDelete[i].entries[j].key;
                delete[] tableToDelete[i].entries[j].value;
                tableToDelete[i].entries[j].vSize = 0;
            }
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

    auto *newTable = new Bucket[newTableSize];
    for (uint_fast64_t i = 0; i < newTableSize; ++i) {
        for (int j = 0; j < BUCKET_SIZE; ++j) {
            newTable[i].entries[j].occupied = false;
            newTable[i].entries[j].compressed = false;
        }
    }

    #pragma omp parallel for schedule(dynamic)
    for (uint_fast64_t i = 0; i < tableSize; ++i) {
        for (int j = 0; j < BUCKET_SIZE; ++j) {
            if (table[i].entries[j].occupied) {
                auto kSize = strlen(table[i].entries[j].key) + 1;
                auto vSize = strlen(table[i].entries[j].value) + 1;
                uint_fast64_t attempt = 0;
                uint_fast64_t idx;
                uint_fast64_t primaryHash = hashFunc(table[i].entries[j].key);

                bool inserted = false;
                do {
                    idx = calcIndex(primaryHash, attempt++, newTableSize);
                    for (int k = 0; k < BUCKET_SIZE; ++k) {
                        if (!newTable[idx].entries[k].occupied) {
                            newTable[idx].entries[k].key = new char[kSize];
                            newTable[idx].entries[k].value = new char[vSize];
                            newTable[idx].entries[k].vSize = vSize;
                            memcpy(newTable[idx].entries[k].key, table[i].entries[j].key, kSize);
                            memcpy(newTable[idx].entries[k].value, table[i].entries[j].value, vSize);
                            newTable[idx].entries[k].occupied = true;
                            newTable[idx].entries[k].compressed = table[i].entries[j].compressed;
                            inserted = true;
                            delete[] table[i].entries[j].key;
                            delete[] table[i].entries[j].value;
                            table[i].entries[j].vSize = 0;
                            break;
                        }
                    }
                } while (!inserted && attempt < MAX_READ_WRITE_ATTEMPTS);
                if (!inserted) {
                    std::cerr << "Resize Error: Could not insert key during migration." << std::endl;
                }
            }
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
    std::cout << "Resizing finished in " << duration.count() << " ms ! numEntries = " << numEntries << " tableSize = " << tableSize << std::endl;
#endif
}

bool kvs::KeyValueStore::set(const char *key, const char *value, uint_fast64_t hash)
{
    if (numEntries >= ((tableSize * RESIZE_THRESHOLD_PERCENTAGE) / 100) && !isResizing) {
        resize();
    }

    uint_fast64_t attempt = 0;
    uint_fast64_t idx;
    auto kSize = strlen(key) + 1;
    auto vSize = strlen(value) + 1;
    do {
        idx = calcIndex(hash, attempt++, tableSize);
        for (int i = 0; i < BUCKET_SIZE; ++i) {
            auto& tableEntry = table[idx].entries[i];
            if (!tableEntry.occupied) {
                tableEntry.key = new char[kSize];
                memcpy(tableEntry.key, key, kSize);

                if (compressionEnabled && vSize >= MIN_SIZE_TO_COMPRESS) { // TODO: extract into function, this is similar to line 186
                    auto compressed = GzipCompressor::Compress(value);
                    if (compressed.operationResult == 0) {
                        tableEntry.value = compressed.data;
                        tableEntry.vSize = compressed.size;
                        tableEntry.compressed = true;
                    } else {
                        tableEntry.value = new char[vSize];
                        tableEntry.vSize = vSize;
                        memcpy(tableEntry.value, value, vSize);
                    }
                } else {
                    tableEntry.value = new char[vSize];
                    tableEntry.vSize = vSize;
                    memcpy(tableEntry.value, value, vSize);
                }

                tableEntry.occupied = true;
                ++numEntries;
                return true;
            } else if (strcmp(tableEntry.key, key) == 0) {
                delete[] tableEntry.value;
                tableEntry.compressed = false;
                if (compressionEnabled && vSize >= MIN_SIZE_TO_COMPRESS) {
                    auto compressed = GzipCompressor::Compress(value);
                    if (compressed.operationResult == 0) {
                        tableEntry.value = compressed.data;
                        tableEntry.vSize = compressed.size;
                        tableEntry.compressed = true;
                    } else {
                        tableEntry.value = new char[vSize];
                        tableEntry.vSize = vSize;
                        memcpy(tableEntry.value, value, vSize);
                    }
                } else {
                    tableEntry.value = new char[vSize];
                    tableEntry.vSize = vSize;
                    memcpy(tableEntry.value, value, vSize);
                }
                return true;
            }
        }
        numCollisions++;
    } while (attempt < MAX_READ_WRITE_ATTEMPTS);

#ifndef NDEBUG
    std::cerr << "Failed to insert key = " << key << " after " << attempt << " attempts." << std::endl;
#endif
    return false;
}

bool KeyValueStore::set(const char *key, const char *value) {
    auto primaryHash = hashFunc(key);
    return set(key, value, primaryHash);
}

const char* kvs::KeyValueStore::get(const char *key, uint_fast64_t hash)
{
    uint_fast64_t attempt = 0;
    uint_fast64_t idx;
    do {
        idx = calcIndex(hash, attempt++, tableSize);
        for (int i = 0; i < BUCKET_SIZE; ++i) {
            auto& tableEntry = table[idx].entries[i];
            if (tableEntry.occupied && strcmp(tableEntry.key, key) == 0) {
                if (tableEntry.compressed) {
                    auto decompressed = GzipCompressor::Decompress(tableEntry.value, tableEntry.vSize);
                    if (decompressed.operationResult == 0) {
                        return decompressed.data;
                    }
                    return nullptr;
                }
                return tableEntry.value;
            }
        }
    } while (attempt < MAX_READ_WRITE_ATTEMPTS);

    return nullptr;
}

const char* KeyValueStore::get(const char *key) {
    auto primaryHash = hashFunc(key);
    return get(key, primaryHash);
}
