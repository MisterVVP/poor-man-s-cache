#include "kvs.h"

KeyValueStore::KeyValueStore(KeyValueStoreSettings settings)
    : tableSize(settings.initialSize),
      numEntries(0),
      numCollisions(0),
      numResizes(0),
      isResizing(false),
      compressionEnabled(settings.compressionEnabled),
      usePrimeNumbers(settings.usePrimeNumbers) {

    std::cout << "Table initialization started! initialSize = " << tableSize << " usePrimeNumbers = " << usePrimeNumbers << " compressionEnabled = " << compressionEnabled << std::endl;
    table = new Bucket[tableSize];
    for (uint_fast64_t i = 0; i < tableSize; ++i) {
        for (int j = 0; j < BUCKET_SIZE; ++j) {
            table[i].entries[j].occupied = false;
        }
    }
    if (usePrimeNumbers) {
        generatePrimeQueue();
    }

    if (compressionEnabled) {
        KeyValueStoreSettings dictSettings;
        dictSettings.usePrimeNumbers = false;
        dictSettings.compressionEnabled = false;
        dictSettings.initialSize = FREQ_DICT_SIZE;
        compressDictionary = new KeyValueStore(dictSettings);
    }
    std::cout << "Table initialization finished!" << std::endl;
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
    std::cout << "Key value storage is being destroyed! numCollisions = " << numCollisions << " emptyBuckets = "
              << emptyBuckets << " emptyEntries = " << emptyEntries << " tableSize = " << tableSize 
              << " numEntries = " << numEntries << " numResizes = " << numResizes << std::endl;
    cleanTable(table, tableSize, true);
    if (compressionEnabled) {        
        delete compressDictionary;
    }
}

bool KeyValueStore::isPrime(uint_fast64_t n) const {
    if (n < 2) return false;
    if (n % 2 == 0 && n > 2) return false;
    for (uint_fast64_t i = 3; i <= std::sqrt(n); i += 2) {
        if (n % i == 0) return false;
    }
    return true;
}

uint_fast64_t KeyValueStore::nextPrime(uint_fast64_t start) const {
    while (!isPrime(start)) {
        ++start;
    }
    return start;
}

void KeyValueStore::generatePrimeQueue() {
    uint_fast64_t prime = 2053; // Start with the first prime
    primeQueue.push(prime);

    double growthFactor = 2.0; // Start with a higher growth factor for small primes
    while (prime < std::numeric_limits<uint_fast64_t>::max() / 10) {
        uint_fast64_t nextCandidate = static_cast<uint_fast64_t>(prime * growthFactor);
        prime = nextPrime(nextCandidate); // Get the next prime >= nextCandidate
        primeQueue.push(prime);

        // Dynamically adjust the growth factor
        if (prime < 100000) {
            growthFactor = 4;
        } else if (prime < 100000) {
            growthFactor = 2; // Faster growth for small primes
        } else if (prime < 1000000) {
            growthFactor = 1.5; // Moderate growth for medium primes
        } else if (prime < 10000000) {
            growthFactor = 1.2; // Slower growth for larger primes
        } else if (prime < 100000000) {
            growthFactor = 1.1; // Moderate growth for medium primes
        } else {
            growthFactor = 1.05; // Very slow growth for very large primes
        }
    }
}
void KeyValueStore::cleanTable(Bucket *tableToDelete, uint_fast64_t size, bool dropTable) {
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

//-----------------------------------------------------------------------------
// MurmurHash3 was written by Austin Appleby, and is placed in the public
// domain. The author hereby disclaims copyright to this function source code.
//-----------------------------------------------------------------------------
uint_fast64_t KeyValueStore::hash(const char *key) const {
    uint_fast64_t hash(525201411107845655ull);
    for (; *key; ++key) {
        hash ^= *key;
        hash *= 0x5bd1e9955bd1e995;
        hash ^= hash >> 47;
    }
    return hash;
}
//-----------------------------------------------------------------------------

// Performs value compression and allocates new value in memory
char* KeyValueStore::compress(const char* value) {
    if (!value || value[0] == '\0') {
        return nullptr;
    }
    const size_t valueLen = strlen(value);
    char* compressedResult = new char[valueLen];

    size_t compressedPos = 0;
    size_t i = 0;

    while (i < valueLen) {
        bool matchFound = false;

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
                        matchFound = true;
                        break;
                }
            }
        }

        if (!matchFound) {
            // No match found, copy current character
            compressedResult[compressedPos++] = value[i++];
        }
    }
    compressedResult[compressedPos] = '\0';
    
    if (strlen(compressedResult) < valueLen) {
        char* result = new char[strlen(compressedResult) + 1];
        strcpy(result, compressedResult);
        delete[] compressedResult;
        return result;
    } else {
        delete[] compressedResult;        
        return nullptr;
    }

}

// Performs value decompression and allocates new value in memory
char* KeyValueStore::decompress(const char* compressedValue) {
    if (!compressedValue || compressedValue[0] == '\0') {
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

    decompressedResult[decompressedPos] = '\0'; // Null-terminate
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
                        bool found = false;
                        for (size_t j = 0; j < frequencyCount; ++j) {
                            if (strcmp(frequencies[j].substring, substring) == 0) {
                                ++frequencies[j].count;
                                found = true;
                                delete[] substring;
                                break;
                            }
                        }
                        
                        if (!found) {
                            bool skip = false;
                            for (int k = 0; k < frequencyCount; ++k) {
                                if (std::strstr(frequencies[k].substring, substring) != NULL)
                                {
                                    skip = true;
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
#ifndef NDEBUG
                std::cout << "Adding to compression dictionary: key = " << std::to_string(i).c_str() << " value = " << frequencies[i].substring
                          << " freq count = " << frequencies[i].count << std::endl;
#endif
                compressDictionary->set(std::to_string(i).c_str(), frequencies[i].substring);
        }
    }

    for (size_t i = 0; i < frequencyCount; ++i) {
        delete[] frequencies[i].substring;
    }

#ifndef NDEBUG
    std::cout << "Compression dictionary content: " << std::endl;
    for (uint_fast64_t i = 0; i < compressDictionary->tableSize; ++i) {
        for (int j = 0; j < BUCKET_SIZE; ++j) {
            if (compressDictionary->table[i].entries[j].occupied) {
                std::cout << "compressDictionary->table[i].entries[j].key = " << compressDictionary->table[i].entries[j].key 
                          << " compressDictionary->table[i].entries[j].value = " << compressDictionary->table[i].entries[j].value << std::endl;
            }
        }
    }
#endif
}

void KeyValueStore::resize() {
    isResizing = true;
#ifndef NDEBUG
    std::cout << "Resizing started! numEntries = " << numEntries << " tableSize = " << tableSize << std::endl;
#endif
    uint_fast64_t newTableSize;
    if (usePrimeNumbers) {
        if (primeQueue.empty()) {
            throw std::runtime_error("Max hashtable size reached. Cannot resize further.");
        }
        newTableSize = primeQueue.front();
        primeQueue.pop();
    } else {
        newTableSize =  tableSize * 2;
    }

    auto *newTable = new Bucket[newTableSize];
    for (uint_fast64_t i = 0; i < newTableSize; ++i) {
        for (int j = 0; j < BUCKET_SIZE; ++j) {
            newTable[i].entries[j].occupied = false;
        }
    }

    for (uint_fast64_t i = 0; i < tableSize; ++i) {
        for (int j = 0; j < BUCKET_SIZE; ++j) {
            if (table[i].entries[j].occupied) {
                const char* key = table[i].entries[j].key;
                const char* value = table[i].entries[j].value;

                uint_fast64_t attempt = 0;
                uint_fast64_t idx;
                uint_fast64_t primaryHash = hash(key);

                bool inserted = false;
                do {
                    idx = calcIndex(primaryHash, attempt++, newTableSize);
                    for (int k = 0; k < BUCKET_SIZE; ++k) {
                        if (!newTable[idx].entries[k].occupied) {
                            newTable[idx].entries[k].key = new char[strlen(key) + 1];
                            newTable[idx].entries[k].value = new char[strlen(value) + 1];

                            strcpy(newTable[idx].entries[k].key, key);
                            strcpy(newTable[idx].entries[k].value, value);
                            newTable[idx].entries[k].occupied = true;
                            inserted = true;
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

    cleanTable(table, tableSize, true);
    table = newTable;
    tableSize = newTableSize;

    if (compressionEnabled) {
        rebuildCompressionDictionary();
    }
    isResizing = false;
    ++numResizes;
//#ifndef NDEBUG
    std::cout << "Resizing finished! numEntries = " << numEntries << " tableSize = " << tableSize << std::endl;
//#endif
}

bool KeyValueStore::set(const char *key, const char *value) {
    if (numEntries >= ((tableSize * RESIZE_THRESHOLD_PERCENTAGE) / 100) && !isResizing) {
        resize();
    }

    uint_fast64_t attempt = 0;
    uint_fast64_t idx;
    uint_fast64_t primaryHash = hash(key);
    do {
        idx = calcIndex(primaryHash, attempt++, tableSize);
        for (int i = 0; i < BUCKET_SIZE; ++i) {
            if (!table[idx].entries[i].occupied) {

                table[idx].entries[i].key = new char[strlen(key) + 1];
                strcpy(table[idx].entries[i].key, key);

                if (compressionEnabled) {
                    auto compressed = compress(value);
                    if (compressed) {
                        table[idx].entries[i].value = compressed;
                    } else {
                        table[idx].entries[i].value = new char[strlen(value) + 1];
                        strcpy(table[idx].entries[i].value, value);
                    }
                } else {
                    table[idx].entries[i].value = new char[strlen(value) + 1];
                    strcpy(table[idx].entries[i].value, value);
                }

                table[idx].entries[i].occupied = true;
                ++numEntries;
                return true;
            } else if (strcmp(table[idx].entries[i].key, key) == 0) {

                delete[] table[idx].entries[i].value;

                if (compressionEnabled) {
                    auto compressed = compress(value);
                    if (compressed) {
                        table[idx].entries[i].value = compressed;
                    } else {                        
                        table[idx].entries[i].value = new char[strlen(value) + 1];
                        strcpy(table[idx].entries[i].value, value);
                    }
                } else {
                    table[idx].entries[i].value = new char[strlen(value) + 1];
                    strcpy(table[idx].entries[i].value, value);
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

const char* KeyValueStore::get(const char *key) {
    uint_fast64_t attempt = 0;
    uint_fast64_t idx;
    uint_fast64_t primaryHash = hash(key);
    do {
        idx = calcIndex(primaryHash, attempt++, tableSize);
        for (int i = 0; i < BUCKET_SIZE; ++i) {
            if (table[idx].entries[i].occupied && strcmp(table[idx].entries[i].key, key) == 0) {
                return compressionEnabled ? decompress(table[idx].entries[i].value) : table[idx].entries[i].value;
            }
        }
    } while (attempt < MAX_READ_WRITE_ATTEMPTS);

#ifndef NDEBUG
    std::cout << "Unable to find key = " << key << " at idx = " << idx << " numEntries = " << numEntries << " tableSize = " << tableSize  << " attempt = " << attempt << std::endl;
#endif

    return nullptr;
}
