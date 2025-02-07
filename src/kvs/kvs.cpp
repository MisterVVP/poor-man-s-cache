#include "kvs.h"

using namespace kvs;

KeyValueStore::KeyValueStore(KeyValueStoreSettings settings)
    : tableSize(settings.initialSize),
      numEntries(0),
      numCollisions(0),
      numResizes(0),
      isResizing(0),
      compressionEnabled(settings.compressionEnabled),
      usePrimeNumbers(settings.usePrimeNumbers),
      enableTrashcan(settings.enableTrashcan) {
#ifndef NDEBUG
    std::cout << "Table initialization started! initialSize = " << tableSize << " usePrimeNumbers = " << usePrimeNumbers << " compressionEnabled = " << compressionEnabled << std::endl;
#endif
    table = new Bucket[tableSize];
    for (uint_fast64_t i = 0; i < tableSize; ++i) {
        for (int j = 0; j < BUCKET_SIZE; ++j) {
            table[i].entries[j].occupied = 0;
        }
    }

    if (compressionEnabled) {
        enableTrashcan = true; // enable trashcan to avoid memory leaks during decompression
        KeyValueStoreSettings dictSettings;
        dictSettings.usePrimeNumbers = false;
        dictSettings.compressionEnabled = false;
        dictSettings.enableTrashcan = false;
        dictSettings.initialSize = FREQ_DICT_SIZE;
        compressDictionary = std::make_unique<KeyValueStore>(dictSettings);
    }

    if (enableTrashcan) {
        trashcan = std::make_unique<Trashcan<char>>();
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

// Performs value compression and allocates new value in memory
char* KeyValueStore::compress(const char* value) const {
    // Does not make sense to compress nullptr or nothing
    if (!value || value[0] == '\0') {
        return nullptr;
    }
    const size_t valueLen = strlen(value);

    // Does not make sense to compress small data
    if (valueLen < MIN_COMPRESSED_SIZE) {
        return nullptr;
    }

    char* compressedResult = new char[valueLen + 1];
    size_t compressedPos = 0;
    size_t i = 0;

    while (i < valueLen) {
        int_fast8_t matchFound = 0;

        // Check for matches in compressDictionary
        for (size_t dictIndex = 0; dictIndex < compressDictionary->getNumEntries(); ++dictIndex) {
            const char* dictEntry = compressDictionary->get(std::to_string(dictIndex).c_str());
                if (dictEntry) {
                    const size_t entryLen = strlen(dictEntry);
                    if (strncmp(value + i, dictEntry, entryLen) == 0) {
                        // Match found, add index with wrapper characters
                        compressedResult[compressedPos++] = UNIT_SEPARATOR;
                        compressedPos += sprintf(compressedResult + compressedPos, "%zu", dictIndex);
                        compressedResult[compressedPos++] = UNIT_SEPARATOR;
                        i += entryLen;
                        matchFound ^= 1;
                        break;
                }
            }
        }

        if (!matchFound) {
            // No match found, copy current character
            compressedResult[compressedPos++] = value[i++];
        }
    }
    compressedResult[valueLen] = '\0';

    if (compressedPos < valueLen) {
        char* result = new char[compressedPos+1];
        memcpy(result, compressedResult, compressedPos);
        result[compressedPos] = '\0';
        delete[] compressedResult;
        return result;
    } else {
        delete[] compressedResult;        
        return nullptr;
    }

}

// Performs value decompression and allocates new value in memory
char* KeyValueStore::decompress(const char* compressedValue) const{
    if (!compressedValue || compressedValue == nullptr || compressedValue[0] == '\0') {
        return nullptr; // Handle empty values
    }
    const size_t compressedLen = strlen(compressedValue);
    char* decompressedResult = new char[compressedLen*2];
    size_t decompressedPos = 0; // Position in the decompressed buffer
    size_t i = 0;
    while (i < compressedLen) {
        if (compressedValue[i] == UNIT_SEPARATOR) {
            // Extract index between wrapper characters
            ++i; // Skip  UNIT_SEPARATOR
            size_t index = 0;
            while (i < compressedLen && compressedValue[i] != UNIT_SEPARATOR) {
                index = index * 10 + (compressedValue[i++] - '0');
            }
            ++i;
                // Get the corresponding substring from compressDictionary
                const char* dictEntry = compressDictionary->get(std::to_string(index).c_str());
                if (dictEntry) {
                    size_t entryLen = strlen(dictEntry);
                    strncpy(decompressedResult + decompressedPos, dictEntry, entryLen);
                    decompressedPos += entryLen;                
            }
        } else {
            // Copy non-wrapper characters as-is
            decompressedResult[decompressedPos++] = compressedValue[i++];
        }
    }

    decompressedResult[decompressedPos] = '\0';
    return decompressedResult;
}

void KeyValueStore::rebuildCompressionDictionary() {
    SubstringFrequency frequencies[FREQ_DICT_SIZE] = {};
    size_t frequencyCount = 0;

    for (uint_fast64_t i = 0; i < tableSize; ++i) {
        for (int j = 0; j < BUCKET_SIZE; ++j) {
            if (table[i].entries[j].occupied) {
                auto val = table[i].entries[j].value;
                if (strchr(val, UNIT_SEPARATOR) != NULL) // do not compress what is already compressed!
                {                    
                    continue;
                }
                const size_t valueLen = strlen(val);
                
                for (size_t start = 0; start < valueLen; ++start) {
                    for (size_t length = 1; length <= valueLen - start && frequencyCount < FREQ_DICT_SIZE; ++length) {
                        char* substring = new char[length + 1];
                        strncpy(substring, val + start, length);
                        substring[length] = '\0'; // Null-terminate

                        // Check if substring is already in the frequency list
                        int_fast8_t found = 0;
                        for (size_t j = 0; j < frequencyCount; ++j) {
                            if (strcmp(frequencies[j].substring, substring) == 0) {
                                ++frequencies[j].count;
                                found ^= 1;
                                delete[] substring;
                                break;
                            }
                        }
                        
                        if (!found) {
                            int_fast8_t skip = 0;
                            for (int k = 0; k < frequencyCount; ++k) {
                                if (std::strstr(frequencies[k].substring, substring) != NULL)
                                {
                                    skip ^= 1;
                                    break;
                                }
                            }
                            if (skip) {
                                delete[] substring;
                                break;
                            }
                            frequencies[frequencyCount].substring = substring;
                            frequencies[frequencyCount].count = 1;
                            ++frequencyCount;
                        }
                    }
                }
            }
        }
    }


    std::sort(frequencies, frequencies + frequencyCount, [](const SubstringFrequency& a, const SubstringFrequency& b) {
        return a.count * strlen(a.substring) > b.count * strlen(b.substring);
    });

    for (size_t i = 0; i < frequencyCount; ++i) {
        if (frequencies[i].count > 1) {
            compressDictionary->set(std::to_string(i).c_str(), frequencies[i].substring);
        }
    }

    for (size_t i = 0; i < frequencyCount; ++i) {
        delete[] frequencies[i].substring;
    }
}

void KeyValueStore::resize() {
    isResizing ^= 1;
#ifndef NDEBUG
    auto start = std::chrono::high_resolution_clock::now();
    std::cout << "Resizing started! numEntries = " << numEntries << " tableSize = " << tableSize << std::endl;
#endif
    uint_fast64_t newTableSize = usePrimeNumbers ? primegen.PopNext() : tableSize * 2;

    auto *newTable = new Bucket[newTableSize];
    for (uint_fast64_t i = 0; i < newTableSize; ++i) {
        for (int j = 0; j < BUCKET_SIZE; ++j) {
            newTable[i].entries[j].occupied = 0;
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

                int_fast8_t inserted = 0;
                do {
                    idx = calcIndex(primaryHash, attempt++, newTableSize);
                    for (int k = 0; k < BUCKET_SIZE; ++k) {
                        if (!newTable[idx].entries[k].occupied) {
                            newTable[idx].entries[k].key = new char[kSize];
                            newTable[idx].entries[k].value = new char[vSize];
                            memcpy(newTable[idx].entries[k].key, table[i].entries[j].key, kSize);
                            memcpy(newTable[idx].entries[k].value, table[i].entries[j].value, vSize);
                            newTable[idx].entries[k].occupied ^= 1;
                            inserted ^= 1;
                            delete[] table[i].entries[j].key;
                            delete[] table[i].entries[j].value;
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

    if (compressionEnabled && !(numResizes % COMPRESSION_FREQUENCY)) {
#ifndef NDEBUG
    auto rcd_start = std::chrono::high_resolution_clock::now();
#endif
    rebuildCompressionDictionary();
#ifndef NDEBUG
    auto rcd_stop = std::chrono::high_resolution_clock::now();
    auto rcd_dur = std::chrono::duration_cast<std::chrono::milliseconds>(rcd_stop - rcd_start);
    std::cout << "Rebuilding of compression dictionary took " << rcd_dur.count() << " ms !" << std::endl;
#endif
    }
    isResizing ^= 1;
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
            if (!table[idx].entries[i].occupied) {

                table[idx].entries[i].key = new char[kSize];
                memcpy(table[idx].entries[i].key, key, kSize);

                if (compressionEnabled) {
                    auto compressed = compress(value);
                    if (compressed) {
                        table[idx].entries[i].value = compressed;
                    } else {
                        table[idx].entries[i].value = new char[vSize];
                        memcpy(table[idx].entries[i].value, value, vSize);
                    }
                } else {
                    table[idx].entries[i].value = new char[vSize];
                    memcpy(table[idx].entries[i].value, value, vSize);
                }

                table[idx].entries[i].occupied ^= 1;
                ++numEntries;
                return true;
            } else if (strcmp(table[idx].entries[i].key, key) == 0) {

                delete[] table[idx].entries[i].value;

                if (compressionEnabled) {
                    auto compressed = compress(value);
                    if (compressed) {
                        table[idx].entries[i].value = compressed;
                    } else {                        
                        table[idx].entries[i].value = new char[vSize];
                        memcpy(table[idx].entries[i].value, value, vSize);
                    }
                } else {
                    table[idx].entries[i].value = new char[vSize];
                    memcpy(table[idx].entries[i].value, value, vSize);
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

const char *kvs::KeyValueStore::get(const char *key, uint_fast64_t hash)
{
    uint_fast64_t attempt = 0;
    uint_fast64_t idx;
    do {
        idx = calcIndex(hash, attempt++, tableSize);
        for (int i = 0; i < BUCKET_SIZE; ++i) {
            if (table[idx].entries[i].occupied && strcmp(table[idx].entries[i].key, key) == 0) {
                if (compressionEnabled) {
                    auto retVal = decompress(table[idx].entries[i].value);
                    if (enableTrashcan) {
                        trashcan->AddGarbage(retVal);
                    }
                    return retVal;
                }
                return table[idx].entries[i].value;
            }
        }
    } while (attempt < MAX_READ_WRITE_ATTEMPTS);

    return nullptr;
}

const char* KeyValueStore::get(const char *key) {
    auto primaryHash = hashFunc(key);
    return get(key, primaryHash);
}
